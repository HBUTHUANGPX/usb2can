/**
 * @file usb2can_bridge.c
 * @brief USB 负载与 CAN 标准帧之间的桥接实现。
 */

#include "usb2can_bridge.h"

#include <string.h>

/**
 * @brief 返回 CAN FD DLC 对应的实际负载长度。
 *
 * @param dlc 输入 DLC。
 * @return 对应的实际字节数；非法 DLC 返回 0xFF。
 */
static uint8_t usb2can_bridge_canfd_dlc_to_length_internal(uint8_t dlc) {
  if (dlc <= 8U) {
    return dlc;
  }

  switch (dlc) {
    case 9U:
      return 12U;
    case 10U:
      return 16U;
    case 11U:
      return 20U;
    case 12U:
      return 24U;
    case 13U:
      return 32U;
    case 14U:
      return 48U;
    case 15U:
      return 64U;
    default:
      return 0xFFU;
  }
}

/**
 * @brief 计算给定 CAN 帧被编码为协议负载后的长度。
 *
 * @param frame 输入 CAN 标准帧。
 * @return 负载长度；若输入为空或非法则返回 0。
 */
static size_t usb2can_bridge_get_payload_length(
    const Usb2CanStandardFrame* frame) {
  if (frame == NULL || frame->dlc > USB2CAN_CAN_MAX_PAYLOAD_SIZE) {
    return 0U;
  }

  return (size_t)(3U + frame->dlc);
}

/**
 * @brief 计算给定 CAN FD 帧被编码为协议负载后的长度。
 *
 * @param frame 输入 CAN FD 标准帧。
 * @return 负载长度；若输入为空、ID 非法或长度非法则返回 0。
 */
static size_t usb2can_bridge_get_canfd_payload_length(
    const Usb2CanFdStandardFrame* frame) {
  uint8_t dlc = 0U;

  if (frame == NULL || frame->can_id > 0x07FFU) {
    return 0U;
  }
  if (usb2can_bridge_canfd_length_to_dlc(frame->data_length, &dlc) !=
      kUsb2CanStatusOk) {
    return 0U;
  }

  return (size_t)(3U + frame->data_length);
}

/**
 * @brief 计算给定 CAN FD 扩展帧被编码为协议负载后的长度。
 *
 * @param frame 输入 CAN FD 扩展帧。
 * @return 负载长度；若输入为空、ID 非法或长度非法则返回 0。
 */
static size_t usb2can_bridge_get_canfd_ext_payload_length(
    const Usb2CanFdExtendedFrame* frame) {
  uint8_t dlc = 0U;

  if (frame == NULL || frame->can_id > 0x1FFFFFFFUL) {
    return 0U;
  }
  if (usb2can_bridge_canfd_length_to_dlc(frame->data_length, &dlc) !=
      kUsb2CanStatusOk) {
    return 0U;
  }

  return (size_t)(5U + frame->data_length);
}

/**
 * @brief 将协议 data[] 负载解析为一条 CAN 标准帧。
 *
 * 负载采用小端编码：前两个字节保存 11-bit 标准 ID，第三个字节保存 DLC，之后
 * 为 `dlc` 个数据字节。解析过程中会严格限制 `can_id <= 0x7FF` 且 `dlc <= 8`。
 *
 * @param data 输入负载起始地址。
 * @param length 输入负载长度。
 * @param frame 输出的 CAN 标准帧对象。
 * @return 操作状态。
 */
Usb2CanStatus usb2can_bridge_payload_to_can_frame(const uint8_t* data,
                                                  size_t length,
                                                  Usb2CanStandardFrame* frame) {
  uint8_t dlc = 0U;

  if (data == NULL || frame == NULL) {
    return kUsb2CanStatusInvalidArgument;
  }
  if (length < 3U) {
    return kUsb2CanStatusLengthError;
  }

  dlc = data[2];
  if (dlc > USB2CAN_CAN_MAX_PAYLOAD_SIZE) {
    return kUsb2CanStatusLengthError;
  }
  if (length != (size_t)(3U + dlc)) {
    return kUsb2CanStatusLengthError;
  }

  memset(frame, 0, sizeof(*frame));
  frame->can_id = (uint16_t)(data[0] | ((uint16_t)data[1] << 8U));
  if (frame->can_id > 0x07FFU) {
    return kUsb2CanStatusInvalidArgument;
  }

  frame->dlc = dlc;
  if (dlc > 0U) {
    memcpy(frame->payload, &data[3], dlc);
  }

  return kUsb2CanStatusOk;
}

/**
 * @brief 将一条 CAN 标准帧编码为协议 data[] 负载。
 *
 * 编码结果遵循与解析函数完全一致的小端格式，便于主机与设备双向复用同一种
 * 负载结构。
 *
 * @param frame 输入 CAN 标准帧。
 * @param output 输出负载缓冲区。
 * @param output_capacity 输出缓冲区容量。
 * @param output_length 返回编码后的实际长度。
 * @return 操作状态。
 */
Usb2CanStatus usb2can_bridge_can_frame_to_payload(
    const Usb2CanStandardFrame* frame, uint8_t* output, size_t output_capacity,
    size_t* output_length) {
  const size_t payload_length = usb2can_bridge_get_payload_length(frame);

  if (frame == NULL || output == NULL || output_length == NULL) {
    return kUsb2CanStatusInvalidArgument;
  }
  if (payload_length == 0U || frame->can_id > 0x07FFU) {
    return kUsb2CanStatusInvalidArgument;
  }
  if (output_capacity < payload_length) {
    return kUsb2CanStatusBufferTooSmall;
  }

  output[0] = (uint8_t)(frame->can_id & 0xFFU);
  output[1] = (uint8_t)((frame->can_id >> 8U) & 0xFFU);
  output[2] = frame->dlc;
  if (frame->dlc > 0U) {
    memcpy(&output[3], frame->payload, frame->dlc);
  }
  *output_length = payload_length;
  return kUsb2CanStatusOk;
}

Usb2CanStatus usb2can_bridge_canfd_length_to_dlc(uint8_t data_length,
                                                 uint8_t* dlc) {
  if (dlc == NULL) {
    return kUsb2CanStatusInvalidArgument;
  }

  if (data_length <= 8U) {
    *dlc = data_length;
    return kUsb2CanStatusOk;
  }

  switch (data_length) {
    case 12U:
      *dlc = 9U;
      return kUsb2CanStatusOk;
    case 16U:
      *dlc = 10U;
      return kUsb2CanStatusOk;
    case 20U:
      *dlc = 11U;
      return kUsb2CanStatusOk;
    case 24U:
      *dlc = 12U;
      return kUsb2CanStatusOk;
    case 32U:
      *dlc = 13U;
      return kUsb2CanStatusOk;
    case 48U:
      *dlc = 14U;
      return kUsb2CanStatusOk;
    case 64U:
      *dlc = 15U;
      return kUsb2CanStatusOk;
    default:
      return kUsb2CanStatusLengthError;
  }
}

Usb2CanStatus usb2can_bridge_canfd_dlc_to_length(uint8_t dlc,
                                                 uint8_t* data_length) {
  const uint8_t converted_length =
      usb2can_bridge_canfd_dlc_to_length_internal(dlc);

  if (data_length == NULL) {
    return kUsb2CanStatusInvalidArgument;
  }
  if (converted_length == 0xFFU) {
    return kUsb2CanStatusLengthError;
  }

  *data_length = converted_length;
  return kUsb2CanStatusOk;
}

Usb2CanStatus usb2can_bridge_payload_to_canfd_frame(
    const uint8_t* data, size_t length, Usb2CanFdStandardFrame* frame) {
  uint8_t data_length = 0U;

  if (data == NULL || frame == NULL) {
    return kUsb2CanStatusInvalidArgument;
  }
  if (length < 3U) {
    return kUsb2CanStatusLengthError;
  }

  data_length = data[2];
  if (usb2can_bridge_canfd_length_to_dlc(data_length, NULL) !=
      kUsb2CanStatusInvalidArgument) {
    return kUsb2CanStatusInvalidArgument;
  }
  if (usb2can_bridge_canfd_length_to_dlc(data_length, &data_length) !=
      kUsb2CanStatusOk) {
    return kUsb2CanStatusLengthError;
  }
  data_length = data[2];
  if (length != (size_t)(3U + data_length)) {
    return kUsb2CanStatusLengthError;
  }

  memset(frame, 0, sizeof(*frame));
  frame->can_id = (uint16_t)(data[0] | ((uint16_t)data[1] << 8U));
  if (frame->can_id > 0x07FFU) {
    return kUsb2CanStatusInvalidArgument;
  }

  frame->data_length = data_length;
  if (data_length > 0U) {
    memcpy(frame->payload, &data[3], data_length);
  }

  return kUsb2CanStatusOk;
}

Usb2CanStatus usb2can_bridge_canfd_frame_to_payload(
    const Usb2CanFdStandardFrame* frame, uint8_t* output,
    size_t output_capacity, size_t* output_length) {
  const size_t payload_length = usb2can_bridge_get_canfd_payload_length(frame);

  if (frame == NULL || output == NULL || output_length == NULL) {
    return kUsb2CanStatusInvalidArgument;
  }
  if (payload_length == 0U) {
    return kUsb2CanStatusInvalidArgument;
  }
  if (output_capacity < payload_length) {
    return kUsb2CanStatusBufferTooSmall;
  }

  output[0] = (uint8_t)(frame->can_id & 0xFFU);
  output[1] = (uint8_t)((frame->can_id >> 8U) & 0xFFU);
  output[2] = frame->data_length;
  if (frame->data_length > 0U) {
    memcpy(&output[3], frame->payload, frame->data_length);
  }
  *output_length = payload_length;
  return kUsb2CanStatusOk;
}

Usb2CanStatus usb2can_bridge_payload_to_canfd_ext_frame(
    const uint8_t* data, size_t length, Usb2CanFdExtendedFrame* frame) {
  uint8_t data_length = 0U;
  uint8_t dlc = 0U;

  if (data == NULL || frame == NULL) {
    return kUsb2CanStatusInvalidArgument;
  }
  if (length < 5U) {
    return kUsb2CanStatusLengthError;
  }

  data_length = data[4];
  if (usb2can_bridge_canfd_length_to_dlc(data_length, &dlc) !=
      kUsb2CanStatusOk) {
    return kUsb2CanStatusLengthError;
  }
  if (length != (size_t)(5U + data_length)) {
    return kUsb2CanStatusLengthError;
  }

  memset(frame, 0, sizeof(*frame));
  frame->can_id = (uint32_t)data[0] | ((uint32_t)data[1] << 8U) |
                  ((uint32_t)data[2] << 16U) | ((uint32_t)data[3] << 24U);
  if (frame->can_id > 0x1FFFFFFFUL) {
    return kUsb2CanStatusInvalidArgument;
  }

  frame->data_length = data_length;
  if (data_length > 0U) {
    memcpy(frame->payload, &data[5], data_length);
  }

  return kUsb2CanStatusOk;
}

Usb2CanStatus usb2can_bridge_canfd_ext_frame_to_payload(
    const Usb2CanFdExtendedFrame* frame, uint8_t* output,
    size_t output_capacity, size_t* output_length) {
  const size_t payload_length =
      usb2can_bridge_get_canfd_ext_payload_length(frame);

  if (frame == NULL || output == NULL || output_length == NULL) {
    return kUsb2CanStatusInvalidArgument;
  }
  if (payload_length == 0U) {
    return kUsb2CanStatusInvalidArgument;
  }
  if (output_capacity < payload_length) {
    return kUsb2CanStatusBufferTooSmall;
  }

  output[0] = (uint8_t)(frame->can_id & 0xFFU);
  output[1] = (uint8_t)((frame->can_id >> 8U) & 0xFFU);
  output[2] = (uint8_t)((frame->can_id >> 16U) & 0xFFU);
  output[3] = (uint8_t)((frame->can_id >> 24U) & 0xFFU);
  output[4] = frame->data_length;
  if (frame->data_length > 0U) {
    memcpy(&output[5], frame->payload, frame->data_length);
  }
  *output_length = payload_length;
  return kUsb2CanStatusOk;
}
