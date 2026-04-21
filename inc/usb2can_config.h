/**
 * @file usb2can_config.h
 * @brief USB2CAN 工程集中配置项。
 *
 * 该头文件用于集中管理协议头、CAN 波特率、USB 端点地址、字符串描述符等易变
 * 参数，避免这些值散落在多个源文件中，方便后续按项目或板级需求快速调整。
 */

#ifndef USB2CAN_INC_USB2CAN_CONFIG_H_
#define USB2CAN_INC_USB2CAN_CONFIG_H_

#include <stdint.h>

#include "usb2can_types.h"

/** @brief 自定义协议帧头。 */
#define USB2CAN_CONFIG_PROTOCOL_HEAD 0xA5U

/** @brief 默认使用的 USB 设备总线编号。 */
#define USB2CAN_CONFIG_USB_BUS_ID 0U
/** @brief 默认使用的 USB 控制器寄存器基地址。 */
#define USB2CAN_CONFIG_USB_REG_BASE CONFIG_HPM_USBD_BASE

/** @brief CDC IN 端点地址。 */
#define USB2CAN_CONFIG_CDC_IN_EP 0x81U
/** @brief CDC OUT 端点地址。 */
#define USB2CAN_CONFIG_CDC_OUT_EP 0x01U
/** @brief CDC 中断端点地址。 */
#define USB2CAN_CONFIG_CDC_INT_EP 0x83U

/** @brief CDC 接收缓冲区大小。 */
#define USB2CAN_CONFIG_CDC_RX_BUFFER_SIZE 512U
/** @brief CDC 发送缓冲区大小。 */
#define USB2CAN_CONFIG_CDC_TX_BUFFER_SIZE 512U

/** @brief 协议解析工作缓冲区大小。 */
#define USB2CAN_CONFIG_PROTOCOL_FRAME_BUFFER_SIZE 80U
/** @brief 协议发送整包缓冲区大小。 */
#define USB2CAN_CONFIG_PROTOCOL_TX_FRAME_BUFFER_SIZE 80U
/** @brief 协议发送负载缓冲区大小。 */
#define USB2CAN_CONFIG_PROTOCOL_TX_PAYLOAD_BUFFER_SIZE 80U

/** @brief 设备上电后的默认 CAN 通信模式。 */
#define USB2CAN_CONFIG_DEFAULT_MODE kUsb2CanModeCanFdStdBrs
/** @brief CAN 仲裁相位默认波特率。 */
#define USB2CAN_CONFIG_CAN_BAUDRATE 1000000UL
/** @brief CAN 仲裁相位默认采样点，单位为千分比，800 表示 80%。 */
#define USB2CAN_CONFIG_CAN_SAMPLEPOINT_PERMILLE 800U
/** @brief CAN FD 默认数据相位波特率。 */
#define USB2CAN_CONFIG_CANFD_DATA_BAUDRATE 5000000UL
/** @brief CAN FD 数据相位默认采样点，单位为千分比，750 表示 75%。 */
#define USB2CAN_CONFIG_CANFD_DATA_SAMPLEPOINT_PERMILLE 750U
/** @brief CAN FD 默认启用发送延迟补偿，5Mbps BRS 下建议开启。 */
#define USB2CAN_CONFIG_CANFD_ENABLE_TDC 1U
/** @brief CAN FD TDC SSP 偏移；0 表示使用 HPM SDK 按数据段时序自动计算。 */
#define USB2CAN_CONFIG_CANFD_TDC_SSP_OFFSET 0U
/** @brief CAN FD TDC 滤波窗口；0 表示使用 HPM SDK 按数据段时序自动计算。 */
#define USB2CAN_CONFIG_CANFD_TDC_FILTER_WINDOW 0U
/** @brief CAN FD 接收优先布局下的 RXFIFO0 深度。 */
#define USB2CAN_CONFIG_CANFD_RXFIFO0_ELEM_COUNT 28U
/** @brief CAN FD 接收优先布局下保留的 TX FIFO 深度。 */
#define USB2CAN_CONFIG_CANFD_TXFIFO_ELEM_COUNT 4U

/** @brief USB 设备厂商字符串。 */
#define USB2CAN_CONFIG_USB_MANUFACTURER_STRING "HPMicro"
/** @brief USB 设备产品字符串。 */
#define USB2CAN_CONFIG_USB_PRODUCT_STRING "USB2CAN Bridge"
/** @brief USB 设备序列号字符串。 */
#define USB2CAN_CONFIG_USB_SERIAL_STRING "2026033001"
/** @brief USB 设备 BCD 版本号。 */
#define USB2CAN_CONFIG_USB_DEVICE_BCD 0x0100U

#endif  // USB2CAN_INC_USB2CAN_CONFIG_H_
