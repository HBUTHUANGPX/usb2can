/**
 * @file usb2can_protocol_test.c
 * @brief USB2CAN 协议层与桥接层宿主机测试。
 */

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "usb2can_config.h"
#include "usb2can_bridge.h"
#include "usb2can_protocol.h"

/**
 * @brief 断言表达式为真，失败时打印上下文并终止进程。
 *
 * @param condition 需要验证的布尔条件。
 * @param message 断言失败时输出的说明信息。
 */
static void expect_true(bool condition, const char* message) {
  if (!condition) {
    fprintf(stderr, "EXPECT TRUE FAILED: %s\n", message);
    assert(condition);
  }
}

/**
 * @brief 验证协议编码与完整解码流程。
 *
 * 该用例覆盖最典型的数据路径：主机构造一条 CAN 发送命令，协议层负责编码，
 * 然后再由解码接口还原出同样的命令内容。
 */
static void test_protocol_encode_decode_round_trip(void) {
  uint8_t input_payload[] = {0x23, 0x01, 0x04, 0x11, 0x22, 0x33, 0x44};
  uint8_t encoded[32] = {0};
  uint8_t decoded_payload[16] = {0};
  Usb2CanPacket packet = {
      .head = USB2CAN_CONFIG_PROTOCOL_HEAD,
      .cmd = kUsb2CanCommandCanTx,
      .len = (uint16_t)sizeof(input_payload),
      .data = input_payload,
      .data_capacity = (uint16_t)sizeof(input_payload),
  };
  Usb2CanPacket decoded = {
      .data = decoded_payload,
      .data_capacity = (uint16_t)sizeof(decoded_payload),
  };
  size_t encoded_length = 0;

  expect_true(usb2can_protocol_encode(&packet, encoded, sizeof(encoded),
                                      &encoded_length) == kUsb2CanStatusOk,
              "协议编码应成功");
  expect_true(usb2can_protocol_decode(encoded, encoded_length, &decoded) ==
                  kUsb2CanStatusOk,
              "协议解码应成功");
  expect_true(decoded.head == USB2CAN_CONFIG_PROTOCOL_HEAD,
              "解码出的帧头应保持一致");
  expect_true(decoded.cmd == kUsb2CanCommandCanTx, "解码出的命令字应保持一致");
  expect_true(decoded.len == sizeof(input_payload), "解码出的长度应保持一致");
  expect_true(memcmp(decoded.data, input_payload, sizeof(input_payload)) == 0,
              "解码出的负载应与输入一致");
}

/**
 * @brief 验证流式解析器能够跨多个 USB 分片拼出完整协议包。
 *
 * 该用例模拟 CDC OUT 端点把一条协议包拆成两次回调上送的情形，解析器需要在
 * 第一段返回“还需要更多数据”，并在第二段到来后输出完整包。
 */
static void test_protocol_stream_parser_across_chunks(void) {
  uint8_t input_payload[] = {0x56, 0x04, 0xAA, 0xBB, 0xCC, 0xDD};
  uint8_t encoded[32] = {0};
  uint8_t frame_buffer[32] = {0};
  uint8_t parser_payload[16] = {0};
  Usb2CanPacket packet = {
      .head = USB2CAN_CONFIG_PROTOCOL_HEAD,
      .cmd = kUsb2CanCommandCanTx,
      .len = (uint16_t)sizeof(input_payload),
      .data = input_payload,
      .data_capacity = (uint16_t)sizeof(input_payload),
  };
  Usb2CanPacket parsed = {
      .data = parser_payload,
      .data_capacity = (uint16_t)sizeof(parser_payload),
  };
  Usb2CanProtocolParser parser;
  size_t encoded_length = 0;
  size_t consumed_length = 0;

  expect_true(usb2can_protocol_encode(&packet, encoded, sizeof(encoded),
                                      &encoded_length) == kUsb2CanStatusOk,
              "流式解析测试前的协议编码应成功");

  usb2can_protocol_parser_init(&parser, frame_buffer, sizeof(frame_buffer),
                               parser_payload, sizeof(parser_payload));

  expect_true(usb2can_protocol_parser_push(&parser, encoded, 4, &parsed,
                                           &consumed_length) ==
                  kUsb2CanStatusNeedMoreData,
              "仅收到协议头部时应提示继续等待");
  expect_true(consumed_length == 4, "第一段数据应被完整消费");
  expect_true(usb2can_protocol_parser_push(&parser, &encoded[4],
                                           encoded_length - 4, &parsed,
                                           &consumed_length) ==
                  kUsb2CanStatusOk,
              "第二段数据到达后应得到完整协议包");
  expect_true(parsed.len == sizeof(input_payload), "流式解码后的长度应正确");
  expect_true(memcmp(parsed.data, input_payload, sizeof(input_payload)) == 0,
              "流式解码后的负载应正确");
}

/**
 * @brief 验证协议解码能够检测到错误的 CRC。
 *
 * 该用例用于防止解析器把损坏的主机数据错误地发往 CAN 总线。
 */
static void test_protocol_decode_rejects_bad_crc(void) {
  uint8_t input_payload[] = {0x23, 0x01, 0x01, 0x5A};
  uint8_t encoded[32] = {0};
  uint8_t decoded_payload[16] = {0};
  Usb2CanPacket packet = {
      .head = USB2CAN_CONFIG_PROTOCOL_HEAD,
      .cmd = kUsb2CanCommandCanTx,
      .len = (uint16_t)sizeof(input_payload),
      .data = input_payload,
      .data_capacity = (uint16_t)sizeof(input_payload),
  };
  Usb2CanPacket decoded = {
      .data = decoded_payload,
      .data_capacity = (uint16_t)sizeof(decoded_payload),
  };
  size_t encoded_length = 0;

  expect_true(usb2can_protocol_encode(&packet, encoded, sizeof(encoded),
                                      &encoded_length) == kUsb2CanStatusOk,
              "生成损坏包前的编码应成功");
  encoded[USB2CAN_PROTOCOL_HEADER_SIZE + USB2CAN_PROTOCOL_CRC16_SIZE] ^= 0x01U;

  expect_true(usb2can_protocol_decode(encoded, encoded_length, &decoded) ==
                  kUsb2CanStatusChecksumError,
              "损坏数据包应被 CRC 检测拒绝");
}

/**
 * @brief 验证协议负载能够转换为标准 CAN 帧。
 *
 * 该用例覆盖主机下发命令转 CAN 的核心桥接逻辑。
 */
static void test_bridge_converts_payload_to_can_frame(void) {
  const uint8_t payload[] = {0x23, 0x01, 0x03, 0x10, 0x20, 0x30};
  Usb2CanStandardFrame frame;

  expect_true(usb2can_bridge_payload_to_can_frame(payload, sizeof(payload),
                                                  &frame) ==
                  kUsb2CanStatusOk,
              "协议负载转 CAN 帧应成功");
  expect_true(frame.can_id == 0x0123U, "CAN ID 应按小端字节序还原");
  expect_true(frame.dlc == 3U, "DLC 应正确还原");
  expect_true(frame.payload[0] == 0x10U && frame.payload[1] == 0x20U &&
                  frame.payload[2] == 0x30U,
              "CAN 数据区应正确还原");
}

/**
 * @brief 验证桥接层拒绝非法 DLC。
 *
 * 该用例保证桥接层不会把超出 CAN2.0 范围的数据继续向下游传递。
 */
static void test_bridge_rejects_invalid_dlc(void) {
  const uint8_t payload[] = {0x23, 0x01, 0x09, 0, 1, 2, 3, 4, 5, 6, 7, 8};
  Usb2CanStandardFrame frame;

  expect_true(usb2can_bridge_payload_to_can_frame(payload, sizeof(payload),
                                                  &frame) ==
                  kUsb2CanStatusLengthError,
              "DLC 超出 8 时应返回长度错误");
}

/**
 * @brief 宿主机测试程序入口。
 *
 * @return 全部测试通过时返回 0。
 */
int main(void) {
  test_protocol_encode_decode_round_trip();
  test_protocol_stream_parser_across_chunks();
  test_protocol_decode_rejects_bad_crc();
  test_bridge_converts_payload_to_can_frame();
  test_bridge_rejects_invalid_dlc();
  printf("usb2can protocol tests passed.\n");
  return 0;
}
