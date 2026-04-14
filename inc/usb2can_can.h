/**
 * @file usb2can_can.h
 * @brief USB2CAN 的 MCAN 适配层接口。
 */

#ifndef USB2CAN_INC_USB2CAN_CAN_H_
#define USB2CAN_INC_USB2CAN_CAN_H_

#include "usb2can_config.h"
#include "usb2can_types.h"

/**
 * @brief MCAN 适配层的初始化配置。
 */
typedef struct Usb2CanCanConfig {
  /** @brief 经典 CAN 的目标波特率，单位为 bit/s。 */
  uint32_t baudrate;
} Usb2CanCanConfig;

/**
 * @brief CAN 接收回调函数类型。
 *
 * @param frame 最新收到的一条标准 CAN 数据帧。
 */
typedef void (*Usb2CanCanRxCallback)(const Usb2CanStandardFrame* frame);

/**
 * @brief 初始化 MCAN 外设与接收中断。
 *
 * @param rx_callback 收帧后的上报回调。
 * @return 初始化状态。
 */
Usb2CanStatus usb2can_can_init(const Usb2CanCanConfig* config,
                               Usb2CanCanRxCallback rx_callback);

/**
 * @brief 发送一条标准 CAN 数据帧。
 *
 * @param frame 待发送的 CAN 标准帧。
 * @return 发送状态。
 */
Usb2CanStatus usb2can_can_send(const Usb2CanStandardFrame* frame);

#endif  // USB2CAN_INC_USB2CAN_CAN_H_
