/**
 * @file usb2can_app.c
 * @brief USB2CAN 顶层应用编排实现。
 */

#include "usb2can_app.h"

#include <string.h>

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

#include "usb2can_bridge.h"
#include "usb2can_can.h"
#include "usb2can_protocol.h"
#include "usb2can_usb.h"

/** @brief 协议解析使用的帧缓存大小。 */
#define USB2CAN_APP_FRAME_BUFFER_SIZE USB2CAN_CONFIG_PROTOCOL_FRAME_BUFFER_SIZE
/** @brief 协议编码发送缓存大小。 */
#define USB2CAN_APP_TX_BUFFER_SIZE USB2CAN_CONFIG_PROTOCOL_TX_FRAME_BUFFER_SIZE
/** @brief 错误上报负载大小。 */
#define USB2CAN_APP_ERROR_PAYLOAD_SIZE 1U
/** @brief USB 原始输入上报队列深度。 */
#define USB2CAN_APP_USB_RX_QUEUE_LENGTH 8U
/** @brief USB 发送消息队列深度。 */
#define USB2CAN_APP_USB_TX_QUEUE_LENGTH 16U
/** @brief USB 原始输入处理任务优先级。 */
#define USB2CAN_APP_USB_RX_TASK_PRIORITY (configMAX_PRIORITIES - 4U)
/** @brief USB 发送任务优先级。 */
#define USB2CAN_APP_USB_TX_TASK_PRIORITY (configMAX_PRIORITIES - 5U)
/** @brief USB 原始输入处理任务栈大小。 */
#define USB2CAN_APP_USB_RX_TASK_STACK_SIZE (configMINIMAL_STACK_SIZE + 256U)
/** @brief USB 发送任务栈大小。 */
#define USB2CAN_APP_USB_TX_TASK_STACK_SIZE (configMINIMAL_STACK_SIZE + 256U)

/**
 * @brief 发送一条单字节错误上报。
 *
 * @param status 需要上报给主机的错误码。
 */
static void usb2can_app_report_error(Usb2CanStatus status);

/**
 * @brief 将内部状态码映射为主机可见错误码。
 *
 * @param status 内部返回状态。
 * @return 对应的主机侧错误码。
 */
static Usb2CanErrorCode usb2can_app_map_status_to_error_code(
    Usb2CanStatus status);

/**
 * @brief 描述一段待在任务上下文中处理的 USB 原始输入。
 */
typedef struct Usb2CanUsbRxChunk {
  /** @brief 当前分片的有效字节数。 */
  size_t length;
  /** @brief 当前分片的原始字节数据。 */
  uint8_t data[USB2CAN_CONFIG_CDC_RX_BUFFER_SIZE];
} Usb2CanUsbRxChunk;

/**
 * @brief 描述一条待通过 USB 回传给主机的消息。
 */
typedef struct Usb2CanUsbTxMessage {
  /** @brief 是否为错误上报。 */
  bool is_error_report;
  union {
    /** @brief 错误上报时携带的状态码。 */
    Usb2CanStatus status;
    /** @brief CAN 上报时携带的标准帧。 */
    Usb2CanStandardFrame frame;
  };
} Usb2CanUsbTxMessage;

/** @brief 保存顶层应用配置。 */
static Usb2CanAppConfig g_usb2can_app_config;
/** @brief 保存协议解析器实例。 */
static Usb2CanProtocolParser g_usb2can_parser;
/** @brief 协议解析工作缓冲区。 */
static uint8_t g_usb2can_parser_frame_buffer[USB2CAN_APP_FRAME_BUFFER_SIZE];
/** @brief 协议解析得到的数据区缓冲区。 */
static uint8_t g_usb2can_parser_payload_buffer[USB2CAN_APP_FRAME_BUFFER_SIZE];
/** @brief 协议发送时复用的负载缓存。 */
static uint8_t g_usb2can_tx_payload_buffer
    [USB2CAN_CONFIG_PROTOCOL_TX_PAYLOAD_BUFFER_SIZE];
/** @brief 协议发送时复用的整包缓存。 */
static uint8_t g_usb2can_tx_frame_buffer[USB2CAN_APP_TX_BUFFER_SIZE];
/** @brief USB 原始输入队列句柄。 */
static QueueHandle_t g_usb2can_usb_rx_queue = NULL;
/** @brief USB 发送消息队列句柄。 */
static QueueHandle_t g_usb2can_usb_tx_queue = NULL;
/** @brief USB 原始输入 ISR 入队失败计数。 */
static volatile uint32_t g_usb2can_usb_rx_enqueue_fail_count = 0U;
/** @brief CAN 上报 ISR 入队失败计数。 */
static volatile uint32_t g_usb2can_usb_tx_enqueue_fail_count = 0U;
/** @brief CAN 上报成功入队计数。 */
static volatile uint32_t g_usb2can_can_rx_enqueue_count = 0U;

/**
 * @brief 在任务上下文中处理从 USB 收到的原始字节流。
 *
 * 参考 cangaroo_hpmicro 的处理方式，USB 回调只做最小搬运，真正的协议解析与
 * CAN 发送放到普通任务中，避免在 USB 回调上下文里执行阻塞式 CAN 发送。
 *
 * @param parameter 未使用。
 */
static void usb2can_app_usb_rx_task(void* parameter) {
  Usb2CanUsbRxChunk chunk;
  uint32_t debug_rx_chunk_count = 0U;

  (void)parameter;

  for (;;) {
    if (xQueueReceive(g_usb2can_usb_rx_queue, &chunk, portMAX_DELAY) !=
        pdPASS) {
      continue;
    }
    debug_rx_chunk_count++;
    printf("[usb2can][usb-rx-task] chunk=%lu len=%u rx_fail=%lu\n",
           (unsigned long)debug_rx_chunk_count, (unsigned int)chunk.length,
           (unsigned long)g_usb2can_usb_rx_enqueue_fail_count);

    size_t offset = 0U;

    while (offset < chunk.length) {
      Usb2CanPacket packet = {
          .data = g_usb2can_parser_payload_buffer,
          .data_capacity = sizeof(g_usb2can_parser_payload_buffer),
      };
      size_t consumed_length = 0U;
      const Usb2CanStatus parse_status = usb2can_protocol_parser_push(
          &g_usb2can_parser, &chunk.data[offset], chunk.length - offset, &packet,
          &consumed_length);

      offset += consumed_length;
      if (parse_status == kUsb2CanStatusNeedMoreData) {
        printf("[usb2can][usb-rx-task] parser needs more data offset=%u remaining=%u\n",
               (unsigned int)offset,
               (unsigned int)(chunk.length - offset));
        break;
      }
      if (parse_status != kUsb2CanStatusOk) {
        printf("[usb2can][usb-rx-task] parse error=%d\n", (int)parse_status);
        usb2can_app_report_error(parse_status);
        continue;
      }
      if (packet.head != g_usb2can_app_config.protocol_head) {
        printf("[usb2can][usb-rx-task] unsupported head=0x%02X\n", packet.head);
        usb2can_app_report_error(kUsb2CanStatusUnsupported);
        continue;
      }
      if (packet.cmd == kUsb2CanCommandCanTx) {
        Usb2CanStandardFrame frame;
        const Usb2CanStatus bridge_status =
            usb2can_bridge_payload_to_can_frame(packet.data, packet.len, &frame);

        if (bridge_status != kUsb2CanStatusOk) {
          printf("[usb2can][usb-rx-task] payload->can failed status=%d\n",
                 (int)bridge_status);
          usb2can_app_report_error(bridge_status);
          continue;
        }

        printf("[usb2can][usb-rx-task] tx can_id=0x%03X dlc=%u\n", frame.can_id,
               frame.dlc);

        if (usb2can_can_send(&frame) != kUsb2CanStatusOk) {
          printf("[usb2can][usb-rx-task] can send failed\n");
          usb2can_app_report_error(kUsb2CanStatusIoError);
        }
      } else {
        printf("[usb2can][usb-rx-task] unsupported cmd=0x%02X\n", packet.cmd);
        usb2can_app_report_error(kUsb2CanStatusUnsupported);
      }
    }
  }
}

/**
 * @brief 在任务上下文中把 CAN 报文编码并通过 USB 上报给主机。
 *
 * 该任务与 CAN ISR 解耦，参考 cangaroo_hpmicro 的思路，把 USB 发送放到普通任务
 * 中执行，避免在中断上下文里调用阻塞式 CDC 发送接口。
 *
 * @param parameter 未使用。
 */
static void usb2can_app_usb_tx_task(void* parameter) {
  Usb2CanUsbTxMessage message;
  Usb2CanPacket packet;
  size_t payload_length = 0U;
  size_t output_length = 0U;
  uint32_t debug_tx_count = 0U;

  (void)parameter;

  for (;;) {
    if (xQueueReceive(g_usb2can_usb_tx_queue, &message, portMAX_DELAY) !=
        pdPASS) {
      continue;
    }
    debug_tx_count++;
    printf(
        "[usb2can][usb-tx-task] dequeue=%lu kind=%s can_enq=%lu tx_fail=%lu "
        "ready=%d\n",
        (unsigned long)debug_tx_count,
        message.is_error_report ? "error" : "can",
        (unsigned long)g_usb2can_can_rx_enqueue_count,
        (unsigned long)g_usb2can_usb_tx_enqueue_fail_count,
        usb2can_usb_is_ready() ? 1 : 0);
    if (!usb2can_usb_is_ready()) {
      printf("[usb2can][usb-tx-task] drop because usb not ready\n");
      continue;
    }

    if (message.is_error_report) {
      printf("[usb2can][usb-tx-task] report error status=%d\n",
             (int)message.status);
      g_usb2can_tx_payload_buffer[0] =
          (uint8_t)usb2can_app_map_status_to_error_code(message.status);
      packet.head = g_usb2can_app_config.protocol_head;
      packet.cmd = kUsb2CanCommandErrorReport;
      packet.len = USB2CAN_APP_ERROR_PAYLOAD_SIZE;
      packet.data = g_usb2can_tx_payload_buffer;
      packet.data_capacity = USB2CAN_APP_TX_BUFFER_SIZE;
    } else {
      printf("[usb2can][usb-tx-task] send can report can_id=0x%03X dlc=%u\n",
             message.frame.can_id, message.frame.dlc);
      if (usb2can_bridge_can_frame_to_payload(
              &message.frame, g_usb2can_tx_payload_buffer,
              sizeof(g_usb2can_tx_payload_buffer), &payload_length) !=
          kUsb2CanStatusOk) {
        printf("[usb2can][usb-tx-task] can->payload failed\n");
        continue;
      }

      packet.head = g_usb2can_app_config.protocol_head;
      packet.cmd = kUsb2CanCommandCanRxReport;
      packet.len = (uint16_t)payload_length;
      packet.data = g_usb2can_tx_payload_buffer;
      packet.data_capacity = USB2CAN_APP_TX_BUFFER_SIZE;
    }

    if (usb2can_protocol_encode(&packet, g_usb2can_tx_frame_buffer,
                                sizeof(g_usb2can_tx_frame_buffer),
                                &output_length) != kUsb2CanStatusOk) {
      printf("[usb2can][usb-tx-task] protocol encode failed\n");
      continue;
    }

    {
      const Usb2CanStatus send_status =
          usb2can_usb_send(g_usb2can_tx_frame_buffer, output_length);
      printf("[usb2can][usb-tx-task] usb send len=%u status=%d\n",
             (unsigned int)output_length, (int)send_status);
    }
  }
}

/**
 * @brief 将内部状态码映射为主机可见错误码。
 *
 * @param status 内部返回状态。
 * @return 对应的主机侧错误码。
 */
static Usb2CanErrorCode usb2can_app_map_status_to_error_code(
    Usb2CanStatus status) {
  switch (status) {
    case kUsb2CanStatusInvalidArgument:
      return kUsb2CanErrorCodeInvalidArgument;
    case kUsb2CanStatusBufferTooSmall:
      return kUsb2CanErrorCodeBufferTooSmall;
    case kUsb2CanStatusChecksumError:
      return kUsb2CanErrorCodeChecksumError;
    case kUsb2CanStatusLengthError:
      return kUsb2CanErrorCodeLengthError;
    case kUsb2CanStatusNeedMoreData:
      return kUsb2CanErrorCodeNeedMoreData;
    case kUsb2CanStatusUnsupported:
      return kUsb2CanErrorCodeUnsupported;
    case kUsb2CanStatusIoError:
      return kUsb2CanErrorCodeIoError;
    case kUsb2CanStatusOk:
    default:
      return kUsb2CanErrorCodeNone;
  }
}

/**
 * @brief 发送一条单字节错误上报。
 *
 * @param status 需要上报给主机的错误码。
 */
static void usb2can_app_report_error(Usb2CanStatus status) {
  Usb2CanUsbTxMessage message = {
      .is_error_report = true,
      .status = status,
  };

  if (g_usb2can_usb_tx_queue == NULL) {
    return;
  }
  printf("[usb2can][app] queue error report status=%d\n", (int)status);
  (void)xQueueSend(g_usb2can_usb_tx_queue, &message, 0U);
}

/**
 * @brief 初始化 USB2CAN 顶层应用。
 *
 * @param config 应用配置。
 * @return 初始化状态。
 */
Usb2CanStatus usb2can_app_init(const Usb2CanAppConfig* config) {
  Usb2CanCanConfig can_config;

  if (config == NULL) {
    return kUsb2CanStatusInvalidArgument;
  }

  g_usb2can_app_config = *config;
  g_usb2can_usb_rx_queue = xQueueCreate(USB2CAN_APP_USB_RX_QUEUE_LENGTH,
                                        sizeof(Usb2CanUsbRxChunk));
  if (g_usb2can_usb_rx_queue == NULL) {
    printf("usb2can usb rx queue create failed.\n");
    return kUsb2CanStatusIoError;
  }
  g_usb2can_usb_tx_queue = xQueueCreate(USB2CAN_APP_USB_TX_QUEUE_LENGTH,
                                        sizeof(Usb2CanUsbTxMessage));
  if (g_usb2can_usb_tx_queue == NULL) {
    printf("usb2can usb tx queue create failed.\n");
    return kUsb2CanStatusIoError;
  }
  if (xTaskCreate(usb2can_app_usb_rx_task, "usb2can_usb_rx",
                  USB2CAN_APP_USB_RX_TASK_STACK_SIZE, NULL,
                  USB2CAN_APP_USB_RX_TASK_PRIORITY, NULL) != pdPASS) {
    printf("usb2can usb rx task create failed.\n");
    return kUsb2CanStatusIoError;
  }
  if (xTaskCreate(usb2can_app_usb_tx_task, "usb2can_usb_tx",
                  USB2CAN_APP_USB_TX_TASK_STACK_SIZE, NULL,
                  USB2CAN_APP_USB_TX_TASK_PRIORITY, NULL) != pdPASS) {
    printf("usb2can usb tx task create failed.\n");
    return kUsb2CanStatusIoError;
  }
  usb2can_protocol_parser_init(
      &g_usb2can_parser, g_usb2can_parser_frame_buffer,
      sizeof(g_usb2can_parser_frame_buffer), g_usb2can_parser_payload_buffer,
      sizeof(g_usb2can_parser_payload_buffer));

  if (usb2can_usb_init(usb2can_app_on_usb_rx) != kUsb2CanStatusOk) {
    printf("usb2can usb init failed.\n");
    return kUsb2CanStatusIoError;
  }
  can_config.baudrate = g_usb2can_app_config.can_baudrate;
  printf("[usb2can][app] init protocol_head=0x%02X can_baudrate=%lu\n",
         g_usb2can_app_config.protocol_head,
         (unsigned long)g_usb2can_app_config.can_baudrate);
  if (usb2can_can_init(&can_config, usb2can_app_on_can_rx) !=
      kUsb2CanStatusOk) {
    printf("usb2can can init failed.\n");
    return kUsb2CanStatusIoError;
  }

  return kUsb2CanStatusOk;
}

/**
 * @brief 获取工程默认应用配置。
 *
 * @return 一份按集中配置项填充好的默认配置对象。
 */
Usb2CanAppConfig usb2can_app_get_default_config(void) {
  Usb2CanAppConfig config;

  config.protocol_head = USB2CAN_CONFIG_PROTOCOL_HEAD;
  config.can_baudrate = USB2CAN_CONFIG_CAN_BAUDRATE;
  return config;
}

/**
 * @brief 处理一段来自 USB 的原始字节流。
 *
 * 该函数会循环消费输入分片，直到当前缓冲区中的数据全部处理完毕或仍需等待更多
 * 字节；每当解析出一条 `CMD_CAN_TX` 包，就立即转换为一条 CAN 标准帧并发送。
 *
 * @param data 输入字节流。
 * @param length 输入长度。
 */
void usb2can_app_on_usb_rx(const uint8_t* data, size_t length) {
  BaseType_t task_woken = pdFALSE;
  Usb2CanUsbRxChunk chunk;

  if (data == NULL || length == 0U || g_usb2can_usb_rx_queue == NULL) {
    return;
  }
  if (length > sizeof(chunk.data)) {
    return;
  }

  chunk.length = length;
  memcpy(chunk.data, data, length);
  if (xQueueSendFromISR(g_usb2can_usb_rx_queue, &chunk, &task_woken) !=
      pdPASS) {
    g_usb2can_usb_rx_enqueue_fail_count++;
  }
  portYIELD_FROM_ISR(task_woken);
}

/**
 * @brief 处理一条来自 CAN 的标准帧。
 *
 * @param frame 收到的 CAN 标准帧。
 */
void usb2can_app_on_can_rx(const Usb2CanStandardFrame* frame) {
  BaseType_t task_woken = pdFALSE;
  Usb2CanUsbTxMessage message;

  if (frame == NULL) {
    return;
  }
  if (g_usb2can_usb_tx_queue == NULL) {
    return;
  }

  message.is_error_report = false;
  message.frame = *frame;
  if (xQueueSendFromISR(g_usb2can_usb_tx_queue, &message, &task_woken) ==
      pdPASS) {
    g_usb2can_can_rx_enqueue_count++;
  } else {
    g_usb2can_usb_tx_enqueue_fail_count++;
  }
  portYIELD_FROM_ISR(task_woken);
}
