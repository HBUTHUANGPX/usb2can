/**
 * @file usb2can_protocol.c
 * @brief USB2CAN 自定义协议编解码实现。
 */

#include "usb2can_protocol.h"

#include <string.h>

#include "usb2can_config.h"
#include "usb2can_crc.h"

/**
 * @brief 按协议定义从缓冲区读取 16-bit 小端值。
 *
 * @param data 输入缓冲区起始地址。
 * @return 读取出的 16-bit 小端整数。
 */
static uint16_t usb2can_protocol_read_u16_le(const uint8_t* data) {
  return (uint16_t)(data[0] | ((uint16_t)data[1] << 8U));
}

/**
 * @brief 按协议定义向缓冲区写入 16-bit 小端值。
 *
 * @param value 待写入的整数。
 * @param output 输出缓冲区。
 */
static void usb2can_protocol_write_u16_le(uint16_t value, uint8_t* output) {
  output[0] = (uint8_t)(value & 0xFFU);
  output[1] = (uint8_t)((value >> 8U) & 0xFFU);
}

/**
 * @brief 计算一条协议包的总字节长度。
 *
 * @param packet 输入协议包。
 * @return 总字节长度。
 */
static size_t usb2can_protocol_get_packet_size(const Usb2CanPacket* packet) {
  return (size_t)USB2CAN_PROTOCOL_HEADER_SIZE +
         (size_t)USB2CAN_PROTOCOL_CRC16_SIZE + (size_t)packet->len;
}

/**
 * @brief 重置流式解析器到初始空闲状态。
 *
 * @param parser 解析器对象。
 */
static void usb2can_protocol_parser_reset(Usb2CanProtocolParser* parser) {
  parser->frame_length = 0U;
  parser->expected_frame_length = 0U;
}

/**
 * @brief 判断当前解析器是否已经收齐头部字段。
 *
 * @param parser 解析器对象。
 * @return `true` 表示头部字段已收齐。
 */
static bool usb2can_protocol_parser_has_header(
    const Usb2CanProtocolParser* parser) {
  return parser->frame_length >= USB2CAN_PROTOCOL_HEADER_SIZE;
}

/**
 * @brief 将一条协议包编码到输出缓冲区。
 *
 * @param packet 输入协议包视图。
 * @param output 输出字节缓冲区。
 * @param output_capacity 输出缓冲区容量。
 * @param output_length 返回实际写入字节数。
 * @return 操作状态。
 */
Usb2CanStatus usb2can_protocol_encode(const Usb2CanPacket* packet,
                                      uint8_t* output,
                                      size_t output_capacity,
                                      size_t* output_length) {
  size_t packet_size = 0U;

  if (packet == NULL || output == NULL || output_length == NULL) {
    return kUsb2CanStatusInvalidArgument;
  }
  if ((packet->len > 0U) && (packet->data == NULL)) {
    return kUsb2CanStatusInvalidArgument;
  }

  packet_size = usb2can_protocol_get_packet_size(packet);
  if (output_capacity < packet_size) {
    return kUsb2CanStatusBufferTooSmall;
  }

  output[0] = packet->head;
  output[1] = packet->cmd;
  usb2can_protocol_write_u16_le(packet->len, &output[2]);
  output[4] = usb2can_crc8_compute(output, 4U);
  usb2can_protocol_write_u16_le(
      usb2can_crc16_compute(packet->data, packet->len), &output[5]);

  if (packet->len > 0U) {
    memcpy(&output[7], packet->data, packet->len);
  }

  *output_length = packet_size;
  return kUsb2CanStatusOk;
}

/**
 * @brief 对一段完整协议帧进行一次性解码。
 *
 * @param input 输入的完整协议帧。
 * @param input_length 输入字节数。
 * @param packet 输出协议包视图。
 * @return 操作状态。
 */
Usb2CanStatus usb2can_protocol_decode(const uint8_t* input,
                                      size_t input_length,
                                      Usb2CanPacket* packet) {
  uint16_t data_length = 0U;
  uint8_t crc8 = 0U;
  uint16_t crc16 = 0U;
  size_t packet_size = 0U;

  if (input == NULL || packet == NULL || packet->data == NULL) {
    return kUsb2CanStatusInvalidArgument;
  }
  if (input_length < (size_t)USB2CAN_PROTOCOL_HEADER_SIZE +
                         (size_t)USB2CAN_PROTOCOL_CRC16_SIZE) {
    return kUsb2CanStatusLengthError;
  }

  data_length = usb2can_protocol_read_u16_le(&input[2]);
  packet_size = (size_t)USB2CAN_PROTOCOL_HEADER_SIZE +
                (size_t)USB2CAN_PROTOCOL_CRC16_SIZE + (size_t)data_length;
  if (input_length != packet_size) {
    return kUsb2CanStatusLengthError;
  }
  if (packet->data_capacity < data_length) {
    return kUsb2CanStatusBufferTooSmall;
  }

  crc8 = usb2can_crc8_compute(input, 4U);
  if (crc8 != input[4]) {
    return kUsb2CanStatusChecksumError;
  }

  crc16 = usb2can_protocol_read_u16_le(&input[5]);
  if (crc16 != usb2can_crc16_compute(&input[7], data_length)) {
    return kUsb2CanStatusChecksumError;
  }

  packet->head = input[0];
  packet->cmd = input[1];
  packet->len = data_length;
  packet->crc8 = input[4];
  packet->crc16 = crc16;
  if (data_length > 0U) {
    memcpy(packet->data, &input[7], data_length);
  }

  return kUsb2CanStatusOk;
}

/**
 * @brief 初始化流式协议解析器。
 *
 * @param parser 解析器对象。
 * @param frame_buffer 工作缓冲区。
 * @param frame_capacity 工作缓冲区容量。
 * @param data_buffer 解包结果的数据区缓冲区。
 * @param data_capacity 数据区缓冲区容量。
 */
void usb2can_protocol_parser_init(Usb2CanProtocolParser* parser,
                                  uint8_t* frame_buffer,
                                  size_t frame_capacity,
                                  uint8_t* data_buffer,
                                  uint16_t data_capacity) {
  if (parser == NULL) {
    return;
  }

  parser->frame_buffer = frame_buffer;
  parser->frame_capacity = frame_capacity;
  parser->data_buffer = data_buffer;
  parser->data_capacity = data_capacity;
  usb2can_protocol_parser_reset(parser);
}

/**
 * @brief 向流式解析器推入一段新的输入数据。
 *
 * 当前实现按字节扫描帧头并累计完整包长度，适合 USB CDC 收到不定长分片时逐段
 * 拼包。若遇到非法头部或长度超限，解析器会丢弃当前缓存并继续寻找下一帧头。
 *
 * @param parser 解析器对象。
 * @param input 新输入字节流。
 * @param input_length 新输入长度。
 * @param packet 输出协议包。
 * @param consumed_length 返回本次成功消费的输入字节数。
 * @return 解析状态。
 */
Usb2CanStatus usb2can_protocol_parser_push(Usb2CanProtocolParser* parser,
                                           const uint8_t* input,
                                           size_t input_length,
                                           Usb2CanPacket* packet,
                                           size_t* consumed_length) {
  size_t index = 0U;

  if (consumed_length != NULL) {
    *consumed_length = 0U;
  }
  if (parser == NULL || input == NULL || packet == NULL || consumed_length == NULL) {
    return kUsb2CanStatusInvalidArgument;
  }
  if (parser->frame_buffer == NULL || parser->data_buffer == NULL) {
    return kUsb2CanStatusInvalidArgument;
  }

  while (index < input_length) {
    const uint8_t current_byte = input[index];

    if ((parser->frame_length == 0U) &&
        (current_byte != USB2CAN_CONFIG_PROTOCOL_HEAD)) {
      ++index;
      continue;
    }
    if (parser->frame_length >= parser->frame_capacity) {
      usb2can_protocol_parser_reset(parser);
      return kUsb2CanStatusBufferTooSmall;
    }

    parser->frame_buffer[parser->frame_length++] = current_byte;
    ++index;

    if (usb2can_protocol_parser_has_header(parser) &&
        parser->expected_frame_length == 0U) {
      const uint16_t data_length =
          usb2can_protocol_read_u16_le(&parser->frame_buffer[2]);
      parser->expected_frame_length = (size_t)USB2CAN_PROTOCOL_HEADER_SIZE +
                                      (size_t)USB2CAN_PROTOCOL_CRC16_SIZE +
                                      (size_t)data_length;
      if ((data_length > parser->data_capacity) ||
          (parser->expected_frame_length > parser->frame_capacity)) {
        usb2can_protocol_parser_reset(parser);
        *consumed_length = index;
        return kUsb2CanStatusLengthError;
      }
    }

    if ((parser->expected_frame_length != 0U) &&
        (parser->frame_length == parser->expected_frame_length)) {
      packet->data = parser->data_buffer;
      packet->data_capacity = parser->data_capacity;
      *consumed_length = index;

      {
        const Usb2CanStatus decode_status = usb2can_protocol_decode(
            parser->frame_buffer, parser->frame_length, packet);
        usb2can_protocol_parser_reset(parser);
        return decode_status;
      }
    }
  }

  *consumed_length = index;
  return kUsb2CanStatusNeedMoreData;
}
