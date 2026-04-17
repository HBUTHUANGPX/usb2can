/**
 * @file usb2can_bridge_test.c
 * @brief USB2CAN 桥接层宿主机测试。
 */

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

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

int main(void) {
  test_canfd_length_mapping_accepts_canonical_sizes();
  test_canfd_length_mapping_rejects_non_canonical_sizes();
  printf("usb2can bridge tests passed.\n");
  return 0;
}
