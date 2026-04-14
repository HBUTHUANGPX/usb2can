/**
 * @file usb2can_crc.c
 * @brief USB2CAN 协议使用的 CRC 计算实现。
 */

#include "usb2can_crc.h"

/** @brief CRC8 的初始值。 */
static const uint8_t kUsb2CanCrc8Init = 0xFFU;
/** @brief CRC8 计算使用的多项式。 */
static const uint8_t kUsb2CanCrc8Polynomial = 0x31U;
/** @brief CRC16 的初始值。 */
static const uint16_t kUsb2CanCrc16Init = 0xFFFFU;
/** @brief CRC16 计算使用的多项式。 */
static const uint16_t kUsb2CanCrc16Polynomial = 0x1021U;

/**
 * @brief 计算协议头部使用的 CRC8。
 *
 * 当前实现采用逐位移位算法，优先保证逻辑清晰和可验证性，适合本项目较短的头部
 * 字段校验场景；若后续需要进一步提升吞吐量，可以再替换为查表版本。
 *
 * @param data 指向待计算的输入数据。
 * @param length 输入数据长度。
 * @return 计算得到的 CRC8 值。
 */
uint8_t usb2can_crc8_compute(const uint8_t* data, size_t length) {
  uint8_t crc = kUsb2CanCrc8Init;

  if (data == NULL) {
    return crc;
  }

  for (size_t index = 0; index < length; ++index) {
    crc ^= data[index];
    for (uint8_t bit = 0; bit < 8U; ++bit) {
      if ((crc & 0x80U) != 0U) {
        crc = (uint8_t)((crc << 1U) ^ kUsb2CanCrc8Polynomial);
      } else {
        crc <<= 1U;
      }
    }
  }

  return crc;
}

/**
 * @brief 计算协议数据区使用的 CRC16。
 *
 * 该实现同样采用逐位移位算法，便于在宿主机测试和 MCU 端保持完全一致的行为。
 *
 * @param data 指向待计算的输入数据。
 * @param length 输入数据长度。
 * @return 计算得到的 CRC16 值。
 */
uint16_t usb2can_crc16_compute(const uint8_t* data, size_t length) {
  uint16_t crc = kUsb2CanCrc16Init;

  if (data == NULL) {
    return crc;
  }

  for (size_t index = 0; index < length; ++index) {
    crc ^= (uint16_t)(data[index] << 8U);
    for (uint8_t bit = 0; bit < 8U; ++bit) {
      if ((crc & 0x8000U) != 0U) {
        crc = (uint16_t)((crc << 1U) ^ kUsb2CanCrc16Polynomial);
      } else {
        crc <<= 1U;
      }
    }
  }

  return crc;
}
