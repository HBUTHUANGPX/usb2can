/**
 * @file usb2can_bridge.h
 * @brief USB 协议负载与 CAN 标准帧之间的转换接口。
 */

#ifndef USB2CAN_INC_USB2CAN_BRIDGE_H_
#define USB2CAN_INC_USB2CAN_BRIDGE_H_

#include "usb2can_types.h"

/**
 * @brief 将协议 data[] 负载解析为一条 CAN 标准帧。
 *
 * @param data 输入负载起始地址。
 * @param length 输入负载长度。
 * @param frame 输出的 CAN 标准帧对象。
 * @return 操作状态。
 */
Usb2CanStatus usb2can_bridge_payload_to_can_frame(const uint8_t* data,
                                                  size_t length,
                                                  Usb2CanStandardFrame* frame);

/**
 * @brief 将一条 CAN 标准帧编码为协议 data[] 负载。
 *
 * @param frame 输入 CAN 标准帧。
 * @param output 输出负载缓冲区。
 * @param output_capacity 输出缓冲区容量。
 * @param output_length 返回编码后的实际长度。
 * @return 操作状态。
 */
Usb2CanStatus usb2can_bridge_can_frame_to_payload(
    const Usb2CanStandardFrame* frame, uint8_t* output, size_t output_capacity,
    size_t* output_length);

/**
 * @brief 将 CAN FD 实际数据长度转换为 MCAN DLC 值。
 *
 * @param data_length CAN FD 实际数据长度。
 * @param dlc 输出的 DLC 值。
 * @return 操作状态。
 */
Usb2CanStatus usb2can_bridge_canfd_length_to_dlc(uint8_t data_length,
                                                 uint8_t* dlc);

/**
 * @brief 将 MCAN DLC 值还原为 CAN FD 实际数据长度。
 *
 * @param dlc 输入的 DLC 值。
 * @param data_length 输出的实际数据长度。
 * @return 操作状态。
 */
Usb2CanStatus usb2can_bridge_canfd_dlc_to_length(uint8_t dlc,
                                                 uint8_t* data_length);

#endif  // USB2CAN_INC_USB2CAN_BRIDGE_H_
