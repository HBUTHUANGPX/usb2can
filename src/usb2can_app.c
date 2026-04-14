/**
 * @file usb2can_app.c
 * @brief USB2CAN 顶层应用编排实现。
 */

#include "usb2can_app.h"

#include <string.h>

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
  Usb2CanPacket packet;
  size_t output_length = 0U;

  if (!usb2can_usb_is_ready()) {
    return;
  }

  g_usb2can_tx_payload_buffer[0] =
      (uint8_t)usb2can_app_map_status_to_error_code(status);
  packet.head = g_usb2can_app_config.protocol_head;
  packet.cmd = kUsb2CanCommandErrorReport;
  packet.len = USB2CAN_APP_ERROR_PAYLOAD_SIZE;
  packet.data = g_usb2can_tx_payload_buffer;
  packet.data_capacity = USB2CAN_APP_TX_BUFFER_SIZE;

  if (usb2can_protocol_encode(&packet, g_usb2can_tx_frame_buffer,
                              sizeof(g_usb2can_tx_frame_buffer),
                              &output_length) == kUsb2CanStatusOk) {
    (void)usb2can_usb_send(g_usb2can_tx_frame_buffer, output_length);
  }
}

/**
 * @brief 将一条 CAN 帧封装为 USB 协议并发往主机。
 *
 * @param frame 需要上报的一条标准 CAN 帧。
 */
static void usb2can_app_send_can_report(const Usb2CanStandardFrame* frame) {
  Usb2CanPacket packet;
  size_t payload_length = 0U;
  size_t output_length = 0U;

  if (!usb2can_usb_is_ready()) {
    return;
  }

  if (usb2can_bridge_can_frame_to_payload(
          frame, g_usb2can_tx_payload_buffer,
          sizeof(g_usb2can_tx_payload_buffer), &payload_length) !=
      kUsb2CanStatusOk) {
    usb2can_app_report_error(kUsb2CanStatusInvalidArgument);
    return;
  }

  packet.head = g_usb2can_app_config.protocol_head;
  packet.cmd = kUsb2CanCommandCanRxReport;
  packet.len = (uint16_t)payload_length;
  packet.data = g_usb2can_tx_payload_buffer;
  packet.data_capacity = USB2CAN_APP_TX_BUFFER_SIZE;

  if (usb2can_protocol_encode(&packet, g_usb2can_tx_frame_buffer,
                              sizeof(g_usb2can_tx_frame_buffer),
                              &output_length) != kUsb2CanStatusOk) {
    usb2can_app_report_error(kUsb2CanStatusBufferTooSmall);
    return;
  }

  (void)usb2can_usb_send(g_usb2can_tx_frame_buffer, output_length);
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
  usb2can_protocol_parser_init(
      &g_usb2can_parser, g_usb2can_parser_frame_buffer,
      sizeof(g_usb2can_parser_frame_buffer), g_usb2can_parser_payload_buffer,
      sizeof(g_usb2can_parser_payload_buffer));

  if (usb2can_usb_init(usb2can_app_on_usb_rx) != kUsb2CanStatusOk) {
    return kUsb2CanStatusIoError;
  }
  can_config.baudrate = g_usb2can_app_config.can_baudrate;
  if (usb2can_can_init(&can_config, usb2can_app_on_can_rx) !=
      kUsb2CanStatusOk) {
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
  size_t offset = 0U;

  while (offset < length) {
    Usb2CanPacket packet = {
        .data = g_usb2can_parser_payload_buffer,
        .data_capacity = sizeof(g_usb2can_parser_payload_buffer),
    };
    size_t consumed_length = 0U;
    const Usb2CanStatus parse_status = usb2can_protocol_parser_push(
        &g_usb2can_parser, &data[offset], length - offset, &packet,
        &consumed_length);

    offset += consumed_length;
    if (parse_status == kUsb2CanStatusNeedMoreData) {
      break;
    }
    if (parse_status != kUsb2CanStatusOk) {
      usb2can_app_report_error(parse_status);
      continue;
    }
    if (packet.head != g_usb2can_app_config.protocol_head) {
      usb2can_app_report_error(kUsb2CanStatusUnsupported);
      continue;
    }
    if (packet.cmd == kUsb2CanCommandCanTx) {
      Usb2CanStandardFrame frame;
      const Usb2CanStatus bridge_status = usb2can_bridge_payload_to_can_frame(
          packet.data, packet.len, &frame);

      if (bridge_status != kUsb2CanStatusOk) {
        usb2can_app_report_error(bridge_status);
        continue;
      }

      if (usb2can_can_send(&frame) != kUsb2CanStatusOk) {
        usb2can_app_report_error(kUsb2CanStatusIoError);
      }
    } else {
      usb2can_app_report_error(kUsb2CanStatusUnsupported);
    }
  }
}

/**
 * @brief 处理一条来自 CAN 的标准帧。
 *
 * @param frame 收到的 CAN 标准帧。
 */
void usb2can_app_on_can_rx(const Usb2CanStandardFrame* frame) {
  if (frame == NULL) {
    usb2can_app_report_error(kUsb2CanStatusInvalidArgument);
    return;
  }

  usb2can_app_send_can_report(frame);
}
