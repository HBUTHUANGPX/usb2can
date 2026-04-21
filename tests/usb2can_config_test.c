/**
 * @file usb2can_config_test.c
 * @brief USB2CAN default configuration host-side tests.
 */

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>

#include "usb2can_config.h"
#include "usb2can_types.h"

static void expect_true(bool condition, const char* message) {
  if (!condition) {
    fprintf(stderr, "EXPECT TRUE FAILED: %s\n", message);
    assert(condition);
  }
}

static void test_default_canfd_brs_bit_timing(void) {
  expect_true(USB2CAN_CONFIG_DEFAULT_MODE == kUsb2CanModeCanFdStdBrs,
              "default MCU mode should be CAN FD standard frame with BRS");
  expect_true(USB2CAN_CONFIG_CAN_BAUDRATE == 1000000UL,
              "nominal/arbitration bitrate should default to 1 Mbps");
  expect_true(USB2CAN_CONFIG_CAN_SAMPLEPOINT_PERMILLE == 800U,
              "nominal/arbitration sample point should default to 80%");
  expect_true(USB2CAN_CONFIG_CANFD_DATA_BAUDRATE == 5000000UL,
              "CAN FD data bitrate should default to 5 Mbps");
  expect_true(USB2CAN_CONFIG_CANFD_DATA_SAMPLEPOINT_PERMILLE == 750U,
              "CAN FD data sample point should default to 75%");
  expect_true(USB2CAN_CONFIG_CANFD_ENABLE_TDC == 1U,
              "CAN FD transmitter delay compensation should be enabled");
  expect_true(USB2CAN_CONFIG_CANFD_TDC_SSP_OFFSET == 0U,
              "CAN FD TDC SSP offset should default to SDK auto calculation");
  expect_true(USB2CAN_CONFIG_CANFD_TDC_FILTER_WINDOW == 0U,
              "CAN FD TDC filter window should default to SDK auto calculation");
}

int main(void) {
  test_default_canfd_brs_bit_timing();
  printf("usb2can_config_test: OK\n");
  return 0;
}
