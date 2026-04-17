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
 * @brief 验证模式枚举、命令字和 CAN FD 帧公共类型已定义。
 *
 * 该用例用于锁定后续主机工具与固件共享的协议常量，避免实现过程中出现两端
 * 对命令字或模式值理解不一致。
 */
static void test_shared_canfd_mode_types_exist(void) {
  Usb2CanFdStandardFrame frame = {0};

  expect_true(kUsb2CanModeCan2Std == 0x00U, "CAN2 标准模式值应固定");
  expect_true(kUsb2CanModeCanFdStd == 0x01U, "CAN FD 标准模式值应固定");
  expect_true(kUsb2CanModeCanFdStdBrs == 0x02U,
              "CAN FD BRS 模式值应固定");
  expect_true(kUsb2CanCommandGetMode == 0x10U, "GET_MODE 命令字应固定");
  expect_true(kUsb2CanCommandGetModeResponse == 0x11U,
              "GET_MODE_RSP 命令字应固定");
  expect_true(kUsb2CanCommandSetMode == 0x12U, "SET_MODE 命令字应固定");
  expect_true(kUsb2CanCommandSetModeResponse == 0x13U,
              "SET_MODE_RSP 命令字应固定");
  expect_true(kUsb2CanCommandGetCapability == 0x14U,
              "GET_CAPABILITY 命令字应固定");
  expect_true(kUsb2CanCommandGetCapabilityResponse == 0x15U,
              "GET_CAPABILITY_RSP 命令字应固定");
  expect_true(kUsb2CanCommandCanTx == 0x01U, "CAN2 TX 命令字应保持兼容");
  expect_true(kUsb2CanCommandCanRxReport == 0x02U,
              "CAN2 RX 上报命令字应保持兼容");
  expect_true(kUsb2CanCommandCanFdTx == 0x03U, "CAN FD TX 命令字应固定");
  expect_true(kUsb2CanCommandCanFdRxReport == 0x04U,
              "CAN FD RX 上报命令字应固定");
  expect_true(sizeof(frame.payload) == 64U, "CAN FD 帧最大负载应为 64 字节");
}

/**
 * @brief 验证控制平面命令可以复用现有协议外壳完成编解码。
 */
static void test_protocol_encode_decode_control_packets(void) {
  uint8_t set_mode_payload[] = {kUsb2CanModeCanFdStd};
  uint8_t capability_payload[] = {0x07U, 0x00U, 0x40U};
  uint8_t encoded[32] = {0};
  uint8_t decoded_payload[8] = {0};
  Usb2CanPacket packet = {
      .head = USB2CAN_CONFIG_PROTOCOL_HEAD,
      .cmd = kUsb2CanCommandSetMode,
      .len = (uint16_t)sizeof(set_mode_payload),
      .data = set_mode_payload,
      .data_capacity = (uint16_t)sizeof(set_mode_payload),
  };
  Usb2CanPacket decoded = {
      .data = decoded_payload,
      .data_capacity = (uint16_t)sizeof(decoded_payload),
  };
  size_t encoded_length = 0U;

  expect_true(usb2can_protocol_encode(&packet, encoded, sizeof(encoded),
                                      &encoded_length) == kUsb2CanStatusOk,
              "SET_MODE 请求编码应成功");
  expect_true(usb2can_protocol_decode(encoded, encoded_length, &decoded) ==
                  kUsb2CanStatusOk,
              "SET_MODE 请求解码应成功");
  expect_true(decoded.cmd == kUsb2CanCommandSetMode,
              "SET_MODE 请求命令字应保持一致");
  expect_true(decoded.len == sizeof(set_mode_payload),
              "SET_MODE 请求负载长度应保持一致");
  expect_true(decoded.data[0] == kUsb2CanModeCanFdStd,
              "SET_MODE 请求负载应保持一致");

  packet.cmd = kUsb2CanCommandGetCapabilityResponse;
  packet.len = (uint16_t)sizeof(capability_payload);
  packet.data = capability_payload;
  packet.data_capacity = (uint16_t)sizeof(capability_payload);
  memset(decoded_payload, 0, sizeof(decoded_payload));

  expect_true(usb2can_protocol_encode(&packet, encoded, sizeof(encoded),
                                      &encoded_length) == kUsb2CanStatusOk,
              "GET_CAPABILITY_RSP 编码应成功");
  expect_true(usb2can_protocol_decode(encoded, encoded_length, &decoded) ==
                  kUsb2CanStatusOk,
              "GET_CAPABILITY_RSP 解码应成功");
  expect_true(decoded.cmd == kUsb2CanCommandGetCapabilityResponse,
              "GET_CAPABILITY_RSP 命令字应保持一致");
  expect_true(decoded.len == sizeof(capability_payload),
              "GET_CAPABILITY_RSP 负载长度应保持一致");
  expect_true(memcmp(decoded.data, capability_payload,
                     sizeof(capability_payload)) == 0,
              "GET_CAPABILITY_RSP 负载应保持一致");
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
  test_shared_canfd_mode_types_exist();
  test_protocol_encode_decode_control_packets();
  printf("usb2can protocol tests passed.\n");
  return 0;
}
