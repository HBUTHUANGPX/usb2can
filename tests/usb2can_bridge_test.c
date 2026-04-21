/**
 * @file usb2can_bridge_test.c
 * @brief USB2CAN 桥接层宿主机测试。
 */

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "usb2can_bridge.h"

static void expect_true(bool condition, const char* message) {
  if (!condition) {
    fprintf(stderr, "EXPECT TRUE FAILED: %s\n", message);
    assert(condition);
  }
}

static void test_canfd_length_mapping_accepts_canonical_sizes(void) {
  uint8_t dlc = 0U;
  uint8_t data_length = 0U;

  expect_true(usb2can_bridge_canfd_length_to_dlc(0U, &dlc) ==
                  kUsb2CanStatusOk &&
                  dlc == 0U,
              "0 字节 CAN FD 长度应映射到 DLC 0");
  expect_true(usb2can_bridge_canfd_length_to_dlc(8U, &dlc) ==
                  kUsb2CanStatusOk &&
                  dlc == 8U,
              "8 字节 CAN FD 长度应映射到 DLC 8");
  expect_true(usb2can_bridge_canfd_length_to_dlc(12U, &dlc) ==
                  kUsb2CanStatusOk &&
                  dlc == 9U,
              "12 字节 CAN FD 长度应映射到 DLC 9");
  expect_true(usb2can_bridge_canfd_length_to_dlc(64U, &dlc) ==
                  kUsb2CanStatusOk &&
                  dlc == 15U,
              "64 字节 CAN FD 长度应映射到 DLC 15");

  expect_true(usb2can_bridge_canfd_dlc_to_length(0U, &data_length) ==
                  kUsb2CanStatusOk &&
                  data_length == 0U,
              "DLC 0 应还原到 0 字节");
  expect_true(usb2can_bridge_canfd_dlc_to_length(8U, &data_length) ==
                  kUsb2CanStatusOk &&
                  data_length == 8U,
              "DLC 8 应还原到 8 字节");
  expect_true(usb2can_bridge_canfd_dlc_to_length(9U, &data_length) ==
                  kUsb2CanStatusOk &&
                  data_length == 12U,
              "DLC 9 应还原到 12 字节");
  expect_true(usb2can_bridge_canfd_dlc_to_length(15U, &data_length) ==
                  kUsb2CanStatusOk &&
                  data_length == 64U,
              "DLC 15 应还原到 64 字节");
}

static void test_canfd_length_mapping_rejects_non_canonical_sizes(void) {
  uint8_t dlc = 0U;
  uint8_t data_length = 0U;

  expect_true(usb2can_bridge_canfd_length_to_dlc(9U, &dlc) ==
                  kUsb2CanStatusLengthError,
              "9 字节 CAN FD 长度应被拒绝");
  expect_true(usb2can_bridge_canfd_length_to_dlc(15U, &dlc) ==
                  kUsb2CanStatusLengthError,
              "15 字节 CAN FD 长度应被拒绝");
  expect_true(usb2can_bridge_canfd_length_to_dlc(63U, &dlc) ==
                  kUsb2CanStatusLengthError,
              "63 字节 CAN FD 长度应被拒绝");
  expect_true(usb2can_bridge_canfd_dlc_to_length(16U, &data_length) ==
                  kUsb2CanStatusLengthError,
              "非法 DLC 应被拒绝");
}

static void test_canfd_payload_round_trip(void) {
  Usb2CanFdStandardFrame input_frame = {
      .can_id = 0x0123U,
      .data_length = 12U,
      .payload = {0x00U, 0x01U, 0x02U, 0x03U, 0x04U, 0x05U,
                  0x06U, 0x07U, 0x08U, 0x09U, 0x0AU, 0x0BU},
  };
  Usb2CanFdStandardFrame decoded_frame;
  uint8_t encoded[80] = {0};
  size_t encoded_length = 0U;

  expect_true(usb2can_bridge_canfd_frame_to_payload(
                  &input_frame, encoded, sizeof(encoded), &encoded_length) ==
                  kUsb2CanStatusOk,
              "CAN FD 帧编码应成功");
  expect_true(encoded_length == 15U, "12 字节 CAN FD 负载编码后长度应为 15");
  expect_true(encoded[0] == 0x23U && encoded[1] == 0x01U,
              "CAN FD 编码结果应包含小端 CAN ID");
  expect_true(encoded[2] == 12U, "CAN FD 编码结果应保存实际数据长度");

  expect_true(usb2can_bridge_payload_to_canfd_frame(encoded, encoded_length,
                                                    &decoded_frame) ==
                  kUsb2CanStatusOk,
              "CAN FD 负载解码应成功");
  expect_true(decoded_frame.can_id == input_frame.can_id,
              "CAN FD 解码后 ID 应一致");
  expect_true(decoded_frame.data_length == input_frame.data_length,
              "CAN FD 解码后长度应一致");
  expect_true(memcmp(decoded_frame.payload, input_frame.payload,
                     input_frame.data_length) == 0,
              "CAN FD 解码后数据区应一致");
}

static void test_canfd_payload_rejects_invalid_values(void) {
  Usb2CanFdStandardFrame invalid_id_frame = {
      .can_id = 0x0800U,
      .data_length = 12U,
  };
  Usb2CanFdStandardFrame invalid_length_frame = {
      .can_id = 0x0123U,
      .data_length = 15U,
  };
  Usb2CanFdStandardFrame decoded_frame;
  uint8_t encoded[80] = {0};
  size_t encoded_length = 0U;
  const uint8_t invalid_payload[] = {0x23U, 0x01U, 0x09U, 0x00U, 0x01U, 0x02U,
                                     0x03U, 0x04U, 0x05U, 0x06U, 0x07U, 0x08U};

  expect_true(usb2can_bridge_canfd_frame_to_payload(
                  &invalid_id_frame, encoded, sizeof(encoded), &encoded_length) ==
                  kUsb2CanStatusInvalidArgument,
              "越界 CAN ID 应被拒绝");
  expect_true(usb2can_bridge_canfd_frame_to_payload(&invalid_length_frame,
                                                    encoded, sizeof(encoded),
                                                    &encoded_length) ==
                  kUsb2CanStatusInvalidArgument,
              "非法 CAN FD 长度应被拒绝");
  expect_true(usb2can_bridge_payload_to_canfd_frame(invalid_payload,
                                                    sizeof(invalid_payload),
                                                    &decoded_frame) ==
                  kUsb2CanStatusLengthError,
              "非法 CAN FD 负载长度应被拒绝");
}

static void test_canfd_extended_payload_round_trip(void) {
  Usb2CanFdExtendedFrame input_frame = {
      .can_id = 0x00008001UL,
      .data_length = 12U,
      .payload = {0x00U, 0x01U, 0x02U, 0x03U, 0x04U, 0x05U,
                  0x06U, 0x07U, 0x08U, 0x09U, 0x0AU, 0x0BU},
  };
  Usb2CanFdExtendedFrame decoded_frame;
  uint8_t encoded[80] = {0};
  size_t encoded_length = 0U;

  expect_true(usb2can_bridge_canfd_ext_frame_to_payload(
                  &input_frame, encoded, sizeof(encoded), &encoded_length) ==
                  kUsb2CanStatusOk,
              "CAN FD 扩展帧编码应成功");
  expect_true(encoded_length == 17U,
              "12 字节 CAN FD 扩展负载编码后长度应为 17");
  expect_true(encoded[0] == 0x01U && encoded[1] == 0x80U &&
                  encoded[2] == 0x00U && encoded[3] == 0x00U,
              "CAN FD 扩展帧编码结果应包含 32-bit 小端 CAN ID");
  expect_true(encoded[4] == 12U, "CAN FD 扩展帧编码结果应保存实际数据长度");

  expect_true(usb2can_bridge_payload_to_canfd_ext_frame(
                  encoded, encoded_length, &decoded_frame) ==
                  kUsb2CanStatusOk,
              "CAN FD 扩展负载解码应成功");
  expect_true(decoded_frame.can_id == input_frame.can_id,
              "CAN FD 扩展解码后 ID 应一致");
  expect_true(decoded_frame.data_length == input_frame.data_length,
              "CAN FD 扩展解码后长度应一致");
  expect_true(memcmp(decoded_frame.payload, input_frame.payload,
                     input_frame.data_length) == 0,
              "CAN FD 扩展解码后数据区应一致");
}

static void test_canfd_extended_payload_rejects_invalid_values(void) {
  Usb2CanFdExtendedFrame invalid_id_frame = {
      .can_id = 0x20000000UL,
      .data_length = 12U,
  };
  Usb2CanFdExtendedFrame invalid_length_frame = {
      .can_id = 0x00008001UL,
      .data_length = 15U,
  };
  Usb2CanFdExtendedFrame decoded_frame;
  uint8_t encoded[80] = {0};
  size_t encoded_length = 0U;
  const uint8_t invalid_id_payload[] = {0x00U, 0x00U, 0x00U, 0x20U, 0x00U};

  expect_true(usb2can_bridge_canfd_ext_frame_to_payload(
                  &invalid_id_frame, encoded, sizeof(encoded),
                  &encoded_length) == kUsb2CanStatusInvalidArgument,
              "超过 29-bit 的扩展 CAN ID 应被拒绝");
  expect_true(usb2can_bridge_canfd_ext_frame_to_payload(
                  &invalid_length_frame, encoded, sizeof(encoded),
                  &encoded_length) == kUsb2CanStatusInvalidArgument,
              "非法 CAN FD 扩展帧长度应被拒绝");
  expect_true(usb2can_bridge_payload_to_canfd_ext_frame(
                  invalid_id_payload, sizeof(invalid_id_payload),
                  &decoded_frame) == kUsb2CanStatusInvalidArgument,
              "超过 29-bit 的扩展 CAN ID 负载应被拒绝");
}

int main(void) {
  test_canfd_length_mapping_accepts_canonical_sizes();
  test_canfd_length_mapping_rejects_non_canonical_sizes();
  test_canfd_payload_round_trip();
  test_canfd_payload_rejects_invalid_values();
  test_canfd_extended_payload_round_trip();
  test_canfd_extended_payload_rejects_invalid_values();
  printf("usb2can bridge tests passed.\n");
  return 0;
}
