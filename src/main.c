/**
 * @file main.c
 * @brief USB2CAN 工程主入口。
 */

#include "FreeRTOS.h"
#include "task.h"

#include <stdio.h>

#include "board.h"
#include "usb_config.h"
#include "usb2can_app.h"

/** @brief 顶层应用初始化任务的优先级。 */
#define USB2CAN_APP_TASK_PRIORITY (configMAX_PRIORITIES - 4U)

/**
 * @brief 执行 USB2CAN 顶层初始化并启动桥接逻辑。
 *
 * 该任务负责在调度器启动后完成 USB、CAN 与协议桥接模块的初始化。当前工程不
 * 需要长期驻留的业务循环，因此初始化完成后任务会自行删除。
 *
 * @param parameter 未使用的任务参数。
 */
static void usb2can_app_task(void* parameter) {
  const Usb2CanAppConfig config = usb2can_app_get_default_config();

  (void)parameter;

  if (usb2can_app_init(&config) != kUsb2CanStatusOk) {
    printf("usb2can app init failed.\n");
    for (;;) {
    }
  }

  printf("usb2can app initialized.\n");
  vTaskDelete(NULL);
}

/**
 * @brief 程序主入口函数。
 *
 * 入口函数完成板级初始化，并创建一个顶层应用任务，让 USB2CAN 模块在
 * FreeRTOS 环境中按统一顺序初始化。
 *
 * @return 理论上不会返回；若返回则为 0。
 */
int main(void) {
  board_init();
  board_init_led_pins();

  printf("usb2can starting.\n");

  if (xTaskCreate(usb2can_app_task, "usb2can_app", 4096U, NULL,
                  USB2CAN_APP_TASK_PRIORITY, NULL) != pdPASS) {
    printf("usb2can task create failed.\n");
    for (;;) {
    }
  }

  vTaskStartScheduler();

  for (;;) {
  }

  return 0;
}
