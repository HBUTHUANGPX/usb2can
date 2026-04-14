/**
 * @file rtconfig.h
 * @brief USB2CAN 工程复用的 RT-Thread 兼容配置入口。
 *
 * CherryUSB 的公共配置会引用该头文件，因此这里保留一个本地转发头，避免工程
 * 直接依赖 SDK 示例目录结构。当前内容完全复用 SDK 示例配置。
 */

#ifndef USB2CAN_CONFIG_RTCONFIG_H_
#define USB2CAN_CONFIG_RTCONFIG_H_

#include "../../../sdk/hpm_sdk/samples/cherryusb/config/rtconfig.h"

#endif  // USB2CAN_CONFIG_RTCONFIG_H_
