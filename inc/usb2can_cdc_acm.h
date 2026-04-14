/**
 * @file usb2can_cdc_acm.h
 * @brief USB2CAN 底层 CDC ACM 设备封装接口。
 */

#ifndef USB2CAN_INC_USB2CAN_CDC_ACM_H_
#define USB2CAN_INC_USB2CAN_CDC_ACM_H_

#include "usb2can_usb.h"

/**
 * @brief 初始化底层 CDC ACM 设备描述符、端点与回调。
 *
 * @param rx_callback USB OUT 数据到达时的上报回调。
 * @return 初始化状态。
 */
Usb2CanStatus usb2can_cdc_acm_init(Usb2CanUsbRxCallback rx_callback);

/**
 * @brief 通过 CDC IN 端点发送一段字节流。
 *
 * @param data 待发送字节流。
 * @param length 发送长度。
 * @return 发送状态。
 */
Usb2CanStatus usb2can_cdc_acm_send(const uint8_t* data, size_t length);

/**
 * @brief 查询底层 CDC ACM 是否已进入可发送状态。
 *
 * @return `true` 表示当前已完成枚举并允许发送。
 */
bool usb2can_cdc_acm_is_ready(void);

#endif  // USB2CAN_INC_USB2CAN_CDC_ACM_H_
