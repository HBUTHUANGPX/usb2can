/**
 * @file usb2can_can.h
 * @brief USB2CAN 的 MCAN 适配层接口。
 */

#ifndef USB2CAN_INC_USB2CAN_CAN_H_
#define USB2CAN_INC_USB2CAN_CAN_H_

#include "usb2can_config.h"
#include "usb2can_types.h"

/**
 * @brief MCAN 适配层的初始化配置。
 */
typedef struct Usb2CanCanConfig {
  /** @brief CAN 仲裁相位目标波特率，单位为 bit/s。 */
  uint32_t baudrate;
  /** @brief CAN 仲裁相位目标采样点，单位为千分比。 */
  uint16_t samplepoint_per_mille;
  /** @brief CAN FD 数据相位波特率，单位为 bit/s。 */
  uint32_t baudrate_fd;
  /** @brief CAN FD 数据相位目标采样点，单位为千分比。 */
  uint16_t samplepoint_fd_per_mille;
  /** @brief CAN FD 是否启用发送延迟补偿。 */
  bool enable_tdc;
  /** @brief CAN FD TDC SSP 偏移，0 表示由 SDK 自动计算。 */
  uint8_t tdc_ssp_offset;
  /** @brief CAN FD TDC 滤波窗口，0 表示由 SDK 自动计算。 */
  uint8_t tdc_filter_window;
  /** @brief 初始活动模式。 */
  Usb2CanMode initial_mode;
} Usb2CanCanConfig;

/**
 * @brief CAN 接收回调函数类型。
 *
 * @param frame 最新收到的一条标准 CAN 数据帧。
 */
typedef void (*Usb2CanCanRxCallback)(const Usb2CanBusFrame* frame);

/**
 * @brief 初始化 MCAN 外设与接收中断。
 *
 * @param rx_callback 收帧后的上报回调。
 * @return 初始化状态。
 */
Usb2CanStatus usb2can_can_init(const Usb2CanCanConfig* config,
                               Usb2CanCanRxCallback rx_callback);

/**
 * @brief 查询 CAN 适配层当前模式。
 *
 * @return 当前模式。
 */
Usb2CanMode usb2can_can_get_mode(void);

/**
 * @brief 重新配置 MCAN 为指定模式。
 *
 * @param mode 目标模式。
 * @return 配置状态。
 */
Usb2CanStatus usb2can_can_reconfigure(Usb2CanMode mode);

/**
 * @brief 发送一条标准 CAN 数据帧。
 *
 * @param frame 待发送的 CAN 标准帧。
 * @return 发送状态。
 */
Usb2CanStatus usb2can_can_send(const Usb2CanStandardFrame* frame);

/**
 * @brief 发送一条 CAN FD 标准数据帧。
 *
 * @param frame 待发送的 CAN FD 标准帧。
 * @param enable_brs 是否启用 BRS。
 * @return 发送状态。
 */
Usb2CanStatus usb2can_can_send_fd(const Usb2CanFdStandardFrame* frame,
                                  bool enable_brs);

#endif  // USB2CAN_INC_USB2CAN_CAN_H_
