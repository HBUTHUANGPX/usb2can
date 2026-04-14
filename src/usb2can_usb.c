/**
 * @file usb2can_usb.c
 * @brief USB2CAN 的 USB CDC 适配层实现。
 */

#include "usb2can_usb.h"

#include "board.h"
#include "usb_config.h"
#include "usb2can_cdc_acm.h"

/** @brief 当前注册的 USB 原始接收回调。 */
static Usb2CanUsbRxCallback g_usb2can_usb_rx_callback = NULL;

/**
 * @brief 供底层 CDC ACM 调用的接收分发函数。
 *
 * @param data 最新收到的一段字节流。
 * @param length 字节流长度。
 */
static void usb2can_usb_dispatch_rx(const uint8_t* data, size_t length) {
  if (g_usb2can_usb_rx_callback != NULL) {
    g_usb2can_usb_rx_callback(data, length);
  }
}

/**
 * @brief 初始化 USB CDC 设备栈。
 *
 * @param rx_callback USB 接收完成后的上报回调。
 * @return 初始化状态。
 */
Usb2CanStatus usb2can_usb_init(Usb2CanUsbRxCallback rx_callback) {
  g_usb2can_usb_rx_callback = rx_callback;

  board_init_usb((USB_Type*)CONFIG_HPM_USBD_BASE);
  intc_set_irq_priority(CONFIG_HPM_USBD_IRQn, 2);

  return usb2can_cdc_acm_init(usb2can_usb_dispatch_rx);
}

/**
 * @brief 通过 USB CDC 向主机发送一段字节流。
 *
 * @param data 待发送字节流。
 * @param length 字节流长度。
 * @return 发送状态。
 */
Usb2CanStatus usb2can_usb_send(const uint8_t* data, size_t length) {
  return usb2can_cdc_acm_send(data, length);
}

/**
 * @brief 查询 USB CDC 当前是否已枚举并可发送数据。
 *
 * @return `true` 表示当前可尝试发送数据。
 */
bool usb2can_usb_is_ready(void) { return usb2can_cdc_acm_is_ready(); }
