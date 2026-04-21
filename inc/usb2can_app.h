/**
 * @file usb2can_app.h
 * @brief USB2CAN 顶层应用编排接口。
 */

#ifndef USB2CAN_INC_USB2CAN_APP_H_
#define USB2CAN_INC_USB2CAN_APP_H_

#include "usb2can_config.h"
#include "usb2can_types.h"

/**
 * @brief 描述 USB2CAN 应用的静态配置。
 *
 * 该配置对象用于把硬件地址、协议头、任务优先级等可变参数与核心逻辑解耦，
 * 便于后续把同一套桥接逻辑迁移到不同板型或不同 USB/CAN 端口。
 */
typedef struct Usb2CanAppConfig {
  /** @brief 设备发送给主机以及主机发送给设备时统一使用的协议帧头。 */
  uint8_t protocol_head;
  /** @brief CAN 仲裁相位默认波特率，单位为 bit/s。 */
  uint32_t can_baudrate;
  /** @brief CAN 仲裁相位默认采样点，单位为千分比。 */
  uint16_t can_samplepoint_per_mille;
  /** @brief CAN FD 数据相位默认波特率，单位为 bit/s。 */
  uint32_t canfd_data_baudrate;
  /** @brief CAN FD 数据相位默认采样点，单位为千分比。 */
  uint16_t canfd_data_samplepoint_per_mille;
  /** @brief 设备上电后的默认 CAN 通信模式。 */
  Usb2CanMode initial_mode;
} Usb2CanAppConfig;

/**
 * @brief 初始化 USB2CAN 顶层应用。
 *
 * @param config 应用配置。
 * @return 初始化状态。
 */
Usb2CanStatus usb2can_app_init(const Usb2CanAppConfig* config);

/**
 * @brief 获取工程默认应用配置。
 *
 * @return 一份按集中配置项填充好的默认配置对象。
 */
Usb2CanAppConfig usb2can_app_get_default_config(void);

/**
 * @brief 处理一段来自 USB 的原始字节流。
 *
 * @param data 输入字节流。
 * @param length 输入长度。
 */
void usb2can_app_on_usb_rx(const uint8_t* data, size_t length);

/**
 * @brief 在中断上下文中上报一条来自 CAN 的总线帧。
 *
 * 该接口供 CAN ISR 调用，只负责把报文转交给后台任务，避免在中断中直接执行 USB
 * 发送等阻塞操作。
 *
 * @param frame 收到的总线帧。
 */
void usb2can_app_on_can_rx(const Usb2CanBusFrame* frame);

#endif  // USB2CAN_INC_USB2CAN_APP_H_
