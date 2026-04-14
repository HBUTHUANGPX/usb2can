/**
 * @file usb2can_usb.h
 * @brief USB2CAN 的 USB CDC 适配层接口。
 */

#ifndef USB2CAN_INC_USB2CAN_USB_H_
#define USB2CAN_INC_USB2CAN_USB_H_

#include "usb2can_types.h"

/**
 * @brief USB 接收回调函数类型。
 *
 * @param data 最新收到的一段原始 USB 字节流。
 * @param length 字节流长度。
 */
typedef void (*Usb2CanUsbRxCallback)(const uint8_t* data, size_t length);

/**
 * @brief 初始化 USB CDC 设备栈。
 *
 * @param rx_callback USB 接收完成后的上报回调。
 * @return 初始化状态。
 */
Usb2CanStatus usb2can_usb_init(Usb2CanUsbRxCallback rx_callback);

/**
 * @brief 通过 USB CDC 向主机发送一段字节流。
 *
 * @param data 待发送字节流。
 * @param length 字节流长度。
 * @return 发送状态。
 */
Usb2CanStatus usb2can_usb_send(const uint8_t* data, size_t length);

/**
 * @brief 查询 USB CDC 当前是否已枚举并可发送数据。
 *
 * @return `true` 表示当前可尝试发送数据。
 */
bool usb2can_usb_is_ready(void);

#endif  // USB2CAN_INC_USB2CAN_USB_H_
