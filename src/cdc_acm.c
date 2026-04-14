/**
 * @file cdc_acm.c
 * @brief USB2CAN 工程使用的 CherryUSB CDC ACM 设备实现。
 */

#include "usb2can_cdc_acm.h"

#include "FreeRTOS.h"
#include "semphr.h"

#include <string.h>

#include "board.h"
#include "usbd_core.h"
#include "usbd_cdc_acm.h"
#include "usb_config.h"
#include "usb2can_config.h"

/** @brief CDC IN 端点地址。 */
#define USB2CAN_CDC_IN_EP USB2CAN_CONFIG_CDC_IN_EP
/** @brief CDC OUT 端点地址。 */
#define USB2CAN_CDC_OUT_EP USB2CAN_CONFIG_CDC_OUT_EP
/** @brief CDC 中断端点地址。 */
#define USB2CAN_CDC_INT_EP USB2CAN_CONFIG_CDC_INT_EP
/** @brief CDC 配置描述符大小。 */
#define USB2CAN_CDC_CONFIG_SIZE (9U + CDC_ACM_DESCRIPTOR_LEN)
/** @brief 底层接收缓冲区大小。 */
#define USB2CAN_CDC_RX_BUFFER_SIZE USB2CAN_CONFIG_CDC_RX_BUFFER_SIZE
/** @brief 底层发送缓冲区大小。 */
#define USB2CAN_CDC_TX_BUFFER_SIZE USB2CAN_CONFIG_CDC_TX_BUFFER_SIZE

/** @brief USB 设备描述符。 */
static const uint8_t kUsb2CanDeviceDescriptor[] = {
    USB_DEVICE_DESCRIPTOR_INIT(USB_2_0, 0xEF, 0x02, 0x01, USBD_VID, USBD_PID,
                               USB2CAN_CONFIG_USB_DEVICE_BCD, 0x01)};

/** @brief 高速配置描述符。 */
static const uint8_t kUsb2CanConfigDescriptorHs[] = {
    USB_CONFIG_DESCRIPTOR_INIT(USB2CAN_CDC_CONFIG_SIZE, 0x02, 0x01,
                               USB_CONFIG_BUS_POWERED, USBD_MAX_POWER),
    CDC_ACM_DESCRIPTOR_INIT(0x00, USB2CAN_CDC_INT_EP, USB2CAN_CDC_OUT_EP,
                            USB2CAN_CDC_IN_EP, USB_BULK_EP_MPS_HS, 0x02),
};

/** @brief 全速配置描述符。 */
static const uint8_t kUsb2CanConfigDescriptorFs[] = {
    USB_CONFIG_DESCRIPTOR_INIT(USB2CAN_CDC_CONFIG_SIZE, 0x02, 0x01,
                               USB_CONFIG_BUS_POWERED, USBD_MAX_POWER),
    CDC_ACM_DESCRIPTOR_INIT(0x00, USB2CAN_CDC_INT_EP, USB2CAN_CDC_OUT_EP,
                            USB2CAN_CDC_IN_EP, USB_BULK_EP_MPS_FS, 0x02),
};

/** @brief 设备限定描述符。 */
static const uint8_t kUsb2CanDeviceQualifierDescriptor[] = {
    USB_DEVICE_QUALIFIER_DESCRIPTOR_INIT(USB_2_0, 0xEF, 0x02, 0x01, 0x01),
};

/** @brief 其他速率下的高速描述符。 */
static const uint8_t kUsb2CanOtherSpeedConfigDescriptorHs[] = {
    USB_OTHER_SPEED_CONFIG_DESCRIPTOR_INIT(USB2CAN_CDC_CONFIG_SIZE, 0x02, 0x01,
                                           USB_CONFIG_BUS_POWERED,
                                           USBD_MAX_POWER),
    CDC_ACM_DESCRIPTOR_INIT(0x00, USB2CAN_CDC_INT_EP, USB2CAN_CDC_OUT_EP,
                            USB2CAN_CDC_IN_EP, USB_BULK_EP_MPS_FS, 0x02),
};

/** @brief 其他速率下的全速描述符。 */
static const uint8_t kUsb2CanOtherSpeedConfigDescriptorFs[] = {
    USB_OTHER_SPEED_CONFIG_DESCRIPTOR_INIT(USB2CAN_CDC_CONFIG_SIZE, 0x02, 0x01,
                                           USB_CONFIG_BUS_POWERED,
                                           USBD_MAX_POWER),
    CDC_ACM_DESCRIPTOR_INIT(0x00, USB2CAN_CDC_INT_EP, USB2CAN_CDC_OUT_EP,
                            USB2CAN_CDC_IN_EP, USB_BULK_EP_MPS_HS, 0x02),
};

/** @brief 字符串描述符表。 */
static const char* kUsb2CanStringDescriptors[] = {
    (const char[]){0x09, 0x04}, USB2CAN_CONFIG_USB_MANUFACTURER_STRING,
    USB2CAN_CONFIG_USB_PRODUCT_STRING, USB2CAN_CONFIG_USB_SERIAL_STRING};

/** @brief 底层接收回调。 */
static Usb2CanUsbRxCallback g_usb2can_cdc_rx_callback = NULL;
/** @brief 标记 USB 是否已经就绪。 */
static volatile bool g_usb2can_cdc_ready = false;
/** @brief 标记主机是否已经打开 DTR。 */
static volatile bool g_usb2can_cdc_dtr = false;
/** @brief 标记 CDC IN 端点当前是否正在发送。 */
static volatile bool g_usb2can_cdc_tx_busy = false;
/** @brief 发送完成同步信号量。 */
static SemaphoreHandle_t g_usb2can_cdc_tx_done = NULL;
/** @brief USB 接口对象。 */
static struct usbd_interface g_usb2can_cdc_intf0;
/** @brief USB 接口对象。 */
static struct usbd_interface g_usb2can_cdc_intf1;
/** @brief OUT 端点双缓冲。 */
USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX static uint8_t g_usb2can_cdc_read_buffer[2]
                                                                              [USB2CAN_CDC_RX_BUFFER_SIZE];
/** @brief IN 端点发送缓冲区。 */
USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX static uint8_t g_usb2can_cdc_write_buffer
    [USB2CAN_CDC_TX_BUFFER_SIZE];
/** @brief 当前接收缓冲区索引。 */
static volatile uint8_t g_usb2can_cdc_read_index = 0U;

/**
 * @brief 获取设备描述符。
 *
 * @param speed 当前总线速率。
 * @return 设备描述符起始地址。
 */
static const uint8_t* usb2can_cdc_device_descriptor_callback(uint8_t speed) {
  (void)speed;
  return kUsb2CanDeviceDescriptor;
}

/**
 * @brief 根据速率获取配置描述符。
 *
 * @param speed 当前总线速率。
 * @return 对应速率下的配置描述符。
 */
static const uint8_t* usb2can_cdc_config_descriptor_callback(uint8_t speed) {
  if (speed == USB_SPEED_HIGH) {
    return kUsb2CanConfigDescriptorHs;
  }
  if (speed == USB_SPEED_FULL) {
    return kUsb2CanConfigDescriptorFs;
  }
  return NULL;
}

/**
 * @brief 获取设备限定描述符。
 *
 * @param speed 当前总线速率。
 * @return 设备限定描述符起始地址。
 */
static const uint8_t* usb2can_cdc_device_qualifier_callback(uint8_t speed) {
  (void)speed;
  return kUsb2CanDeviceQualifierDescriptor;
}

/**
 * @brief 根据速率获取其他速率配置描述符。
 *
 * @param speed 当前总线速率。
 * @return 其他速率配置描述符。
 */
static const uint8_t* usb2can_cdc_other_speed_descriptor_callback(
    uint8_t speed) {
  if (speed == USB_SPEED_HIGH) {
    return kUsb2CanOtherSpeedConfigDescriptorHs;
  }
  if (speed == USB_SPEED_FULL) {
    return kUsb2CanOtherSpeedConfigDescriptorFs;
  }
  return NULL;
}

/**
 * @brief 获取字符串描述符。
 *
 * @param speed 当前总线速率。
 * @param index 描述符索引。
 * @return 字符串描述符指针。
 */
static const char* usb2can_cdc_string_descriptor_callback(uint8_t speed,
                                                          uint8_t index) {
  (void)speed;
  if (index >= (sizeof(kUsb2CanStringDescriptors) /
                sizeof(kUsb2CanStringDescriptors[0]))) {
    return NULL;
  }
  return kUsb2CanStringDescriptors[index];
}

/** @brief USB 描述符入口。 */
static const struct usb_descriptor kUsb2CanCdcDescriptor = {
    .device_descriptor_callback = usb2can_cdc_device_descriptor_callback,
    .config_descriptor_callback = usb2can_cdc_config_descriptor_callback,
    .device_quality_descriptor_callback = usb2can_cdc_device_qualifier_callback,
    .other_speed_descriptor_callback =
        usb2can_cdc_other_speed_descriptor_callback,
    .string_descriptor_callback = usb2can_cdc_string_descriptor_callback,
};

/**
 * @brief USB 设备事件处理函数。
 *
 * @param busid USB 总线编号。
 * @param event USB 设备事件类型。
 */
static void usb2can_cdc_event_handler(uint8_t busid, uint8_t event) {
  switch (event) {
    case USBD_EVENT_CONFIGURED:
      g_usb2can_cdc_ready = true;
      g_usb2can_cdc_read_index = 0U;
      usbd_ep_start_read(busid, USB2CAN_CDC_OUT_EP, &g_usb2can_cdc_read_buffer[0][0],
                         usbd_get_ep_mps(busid, USB2CAN_CDC_OUT_EP));
      break;
    case USBD_EVENT_RESET:
    case USBD_EVENT_DISCONNECTED:
      g_usb2can_cdc_ready = false;
      g_usb2can_cdc_dtr = false;
      g_usb2can_cdc_tx_busy = false;
      if (g_usb2can_cdc_tx_done != NULL) {
        (void)xSemaphoreGive(g_usb2can_cdc_tx_done);
      }
      break;
    default:
      break;
  }
}

/**
 * @brief CDC OUT 端点接收完成回调。
 *
 * @param busid USB 总线编号。
 * @param ep 端点地址。
 * @param nbytes 本次实际接收字节数。
 */
void usbd_cdc_acm_bulk_out(uint8_t busid, uint8_t ep, uint32_t nbytes) {
  const uint8_t index = g_usb2can_cdc_read_index;

  g_usb2can_cdc_read_index = (uint8_t)((index == 0U) ? 1U : 0U);

  if (g_usb2can_cdc_rx_callback != NULL && nbytes > 0U) {
    g_usb2can_cdc_rx_callback(&g_usb2can_cdc_read_buffer[index][0], nbytes);
  }

  /* 收到一段数据后立刻重新挂下一次读请求，保证 CDC OUT 连续工作。 */
  usbd_ep_start_read(busid, ep, &g_usb2can_cdc_read_buffer[g_usb2can_cdc_read_index][0],
                     usbd_get_ep_mps(busid, ep));
}

/**
 * @brief CDC IN 端点发送完成回调。
 *
 * @param busid USB 总线编号。
 * @param ep 端点地址。
 * @param nbytes 本次实际发送字节数。
 */
void usbd_cdc_acm_bulk_in(uint8_t busid, uint8_t ep, uint32_t nbytes) {
  if ((nbytes % usbd_get_ep_mps(busid, ep)) == 0U && nbytes > 0U) {
    usbd_ep_start_write(busid, ep, NULL, 0U);
    return;
  }

  g_usb2can_cdc_tx_busy = false;
  if (g_usb2can_cdc_tx_done != NULL) {
    BaseType_t x_higher_priority_task_woken = pdFALSE;
    xSemaphoreGiveFromISR(g_usb2can_cdc_tx_done, &x_higher_priority_task_woken);
    portYIELD_FROM_ISR(x_higher_priority_task_woken);
  }
}

/** @brief CDC OUT 端点对象。 */
static struct usbd_endpoint g_usb2can_cdc_out_ep = {
    .ep_addr = USB2CAN_CDC_OUT_EP, .ep_cb = usbd_cdc_acm_bulk_out};

/** @brief CDC IN 端点对象。 */
static struct usbd_endpoint g_usb2can_cdc_in_ep = {
    .ep_addr = USB2CAN_CDC_IN_EP, .ep_cb = usbd_cdc_acm_bulk_in};

/**
 * @brief 初始化底层 CDC ACM 设备描述符、端点与回调。
 *
 * @param rx_callback USB OUT 数据到达时的上报回调。
 * @return 初始化状态。
 */
Usb2CanStatus usb2can_cdc_acm_init(Usb2CanUsbRxCallback rx_callback) {
  g_usb2can_cdc_rx_callback = rx_callback;
  g_usb2can_cdc_ready = false;
  g_usb2can_cdc_dtr = false;
  g_usb2can_cdc_tx_busy = false;

  if (g_usb2can_cdc_tx_done == NULL) {
    g_usb2can_cdc_tx_done = xSemaphoreCreateBinary();
    if (g_usb2can_cdc_tx_done == NULL) {
      return kUsb2CanStatusIoError;
    }
  }

  usbd_desc_register(USB2CAN_CONFIG_USB_BUS_ID, &kUsb2CanCdcDescriptor);
  usbd_add_interface(USB2CAN_CONFIG_USB_BUS_ID,
                     usbd_cdc_acm_init_intf(USB2CAN_CONFIG_USB_BUS_ID,
                                            &g_usb2can_cdc_intf0));
  usbd_add_interface(USB2CAN_CONFIG_USB_BUS_ID,
                     usbd_cdc_acm_init_intf(USB2CAN_CONFIG_USB_BUS_ID,
                                            &g_usb2can_cdc_intf1));
  usbd_add_endpoint(USB2CAN_CONFIG_USB_BUS_ID, &g_usb2can_cdc_out_ep);
  usbd_add_endpoint(USB2CAN_CONFIG_USB_BUS_ID, &g_usb2can_cdc_in_ep);

  if (usbd_initialize(USB2CAN_CONFIG_USB_BUS_ID, USB2CAN_CONFIG_USB_REG_BASE,
                      usb2can_cdc_event_handler) !=
      0) {
    return kUsb2CanStatusIoError;
  }

  return kUsb2CanStatusOk;
}

/**
 * @brief 通过 CDC IN 端点发送一段字节流。
 *
 * @param data 待发送字节流。
 * @param length 发送长度。
 * @return 发送状态。
 */
Usb2CanStatus usb2can_cdc_acm_send(const uint8_t* data, size_t length) {
  if (data == NULL || length == 0U) {
    return kUsb2CanStatusInvalidArgument;
  }
  if (length > sizeof(g_usb2can_cdc_write_buffer)) {
    return kUsb2CanStatusBufferTooSmall;
  }
  if (!g_usb2can_cdc_ready) {
    return kUsb2CanStatusIoError;
  }

  while (g_usb2can_cdc_tx_busy) {
    if (xSemaphoreTake(g_usb2can_cdc_tx_done, portMAX_DELAY) != pdTRUE) {
      return kUsb2CanStatusIoError;
    }
  }

  memcpy(g_usb2can_cdc_write_buffer, data, length);
  while (xSemaphoreTake(g_usb2can_cdc_tx_done, 0U) == pdTRUE) {
  }
  g_usb2can_cdc_tx_busy = true;
  if (usbd_ep_start_write(USB2CAN_CONFIG_USB_BUS_ID, USB2CAN_CDC_IN_EP,
                          g_usb2can_cdc_write_buffer,
                          (uint32_t)length) != 0) {
    g_usb2can_cdc_tx_busy = false;
    return kUsb2CanStatusIoError;
  }
  if (xSemaphoreTake(g_usb2can_cdc_tx_done, portMAX_DELAY) != pdTRUE) {
    return kUsb2CanStatusIoError;
  }
  if (!g_usb2can_cdc_ready) {
    return kUsb2CanStatusIoError;
  }

  return kUsb2CanStatusOk;
}

/**
 * @brief 查询底层 CDC ACM 是否已进入可发送状态。
 *
 * @return `true` 表示当前已完成枚举。
 */
bool usb2can_cdc_acm_is_ready(void) {
  return g_usb2can_cdc_ready && g_usb2can_cdc_dtr;
}

/**
 * @brief 响应主机对 DTR 的设置请求。
 *
 * @param busid USB 总线编号。
 * @param intf 接口编号。
 * @param dtr 主机请求的新 DTR 状态。
 */
void usbd_cdc_acm_set_dtr(uint8_t busid, uint8_t intf, bool dtr) {
  (void)busid;
  (void)intf;
  g_usb2can_cdc_dtr = dtr;
  g_usb2can_cdc_ready = dtr;
  if (!dtr) {
    g_usb2can_cdc_tx_busy = false;
    if (g_usb2can_cdc_tx_done != NULL) {
      (void)xSemaphoreGive(g_usb2can_cdc_tx_done);
    }
  }
}
