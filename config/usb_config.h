/**
 * @file usb_config.h
 * @brief USB2CAN 工程复用的 CherryUSB 配置入口。
 *
 * 该文件保持本地工程拥有独立配置入口，便于后续按需覆盖 USB 相关宏定义。
 * 当前版本直接复用 SDK 示例提供的通用 CherryUSB 配置，避免在项目初期重复维护
 * 一份大体一致的配置文件。
 */

#ifndef USB2CAN_CONFIG_USB_CONFIG_H_
#define USB2CAN_CONFIG_USB_CONFIG_H_

#include "../../../sdk/hpm_sdk/samples/cherryusb/config/usb_config.h"

#endif  // USB2CAN_CONFIG_USB_CONFIG_H_
