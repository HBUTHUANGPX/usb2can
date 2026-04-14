/**
 * @file usb2can_crc.h
 * @brief USB2CAN 工程 CRC 计算接口。
 */

#ifndef USB2CAN_INC_USB2CAN_CRC_H_
#define USB2CAN_INC_USB2CAN_CRC_H_

#include "usb2can_types.h"

/**
 * @brief 计算协议头部使用的 CRC8。
 *
 * @param data 指向待计算的输入数据。
 * @param length 输入数据长度，当前典型场景为 4 字节。
 * @return 计算得到的 CRC8 值。
 */
uint8_t usb2can_crc8_compute(const uint8_t* data, size_t length);

/**
 * @brief 计算协议数据区使用的 CRC16。
 *
 * @param data 指向待计算的输入数据。
 * @param length 输入数据长度。
 * @return 计算得到的 CRC16 值。
 */
uint16_t usb2can_crc16_compute(const uint8_t* data, size_t length);

#endif  // USB2CAN_INC_USB2CAN_CRC_H_
