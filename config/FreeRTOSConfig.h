/**
 * @file FreeRTOSConfig.h
 * @brief USB2CAN 工程复用的 FreeRTOS 配置入口。
 *
 * 当前版本沿用 SDK CherryUSB FreeRTOS 示例配置，以便快速建立可编译工程。
 * 当 USB2CAN 后续需要调整任务栈、优先级或内存策略时，可以在本地配置文件中
 * 独立演进，而不影响 SDK 示例源码。
 */

#ifndef USB2CAN_CONFIG_FREERTOS_CONFIG_H_
#define USB2CAN_CONFIG_FREERTOS_CONFIG_H_

#include "../../../sdk/hpm_sdk/samples/cherryusb/config/FreeRTOSConfig.h"

#endif  // USB2CAN_CONFIG_FREERTOS_CONFIG_H_
