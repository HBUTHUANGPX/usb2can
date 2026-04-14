/**
 * @file usb2can_protocol.h
 * @brief USB2CAN 自定义协议编解码接口。
 */

#ifndef USB2CAN_INC_USB2CAN_PROTOCOL_H_
#define USB2CAN_INC_USB2CAN_PROTOCOL_H_

#include "usb2can_types.h"

/**
 * @brief 描述流式协议解析器的运行时状态。
 *
 * 解析器采用“调用方提供固定缓冲区、逐字节吸收数据”的方式工作，避免在中断或
 * USB 回调中动态分配内存。状态机保存当前已接收字节数、期望包长与目标数据缓存。
 */
typedef struct Usb2CanProtocolParser {
  /** @brief 用于缓存完整协议帧原始字节流的工作缓冲区。 */
  uint8_t* frame_buffer;
  /** @brief `frame_buffer` 的容量。 */
  size_t frame_capacity;
  /** @brief 当前已缓存的字节数。 */
  size_t frame_length;
  /** @brief 当前目标协议包的总长度；未知时为 0。 */
  size_t expected_frame_length;
  /** @brief 用户提供的数据区缓冲区。 */
  uint8_t* data_buffer;
  /** @brief 数据区缓冲区容量。 */
  uint16_t data_capacity;
} Usb2CanProtocolParser;

/**
 * @brief 将一条协议包编码到输出缓冲区。
 *
 * @param packet 输入协议包视图，调用前应填写 head/cmd/len/data。
 * @param output 输出字节缓冲区。
 * @param output_capacity 输出缓冲区容量。
 * @param output_length 返回实际写入字节数。
 * @return 操作状态。
 */
Usb2CanStatus usb2can_protocol_encode(const Usb2CanPacket* packet,
                                      uint8_t* output,
                                      size_t output_capacity,
                                      size_t* output_length);

/**
 * @brief 对一段完整协议帧进行一次性解码。
 *
 * @param input 输入的完整协议帧。
 * @param input_length 输入字节数。
 * @param packet 输出协议包视图，调用前需准备好 data/data_capacity。
 * @return 操作状态。
 */
Usb2CanStatus usb2can_protocol_decode(const uint8_t* input,
                                      size_t input_length,
                                      Usb2CanPacket* packet);

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
                                  uint16_t data_capacity);

/**
 * @brief 向流式解析器推入一段新的输入数据。
 *
 * 当返回 `kUsb2CanStatusOk` 时，`packet` 中保存一条完整协议包。若返回
 * `kUsb2CanStatusNeedMoreData`，表示当前输入还不足以拼出完整包。若发生协议错误，
 * 解析器会自动清空内部状态，等待新的包头重新开始。
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
                                           size_t* consumed_length);

#endif  // USB2CAN_INC_USB2CAN_PROTOCOL_H_
