/**
 * @file usb2can_can.c
 * @brief USB2CAN 的 MCAN 适配层实现。
 */

#include "usb2can_can.h"

#include <string.h>

#include "board.h"
#include "hpm_mcan_drv.h"

/** @brief 当前注册的 CAN 接收回调。 */
static Usb2CanCanRxCallback g_usb2can_can_rx_callback = NULL;

#if defined(MCAN_SOC_MSG_BUF_IN_AHB_RAM) && (MCAN_SOC_MSG_BUF_IN_AHB_RAM == 1)
/**
 * @brief MCAN 消息 RAM 缓冲区。
 *
 * MCAN 控制器要求消息 RAM 位于特定 AHB 区域时，需要显式放置该缓冲区。
 */
ATTR_PLACE_AT(".ahb_sram") static uint32_t g_usb2can_can_msg_buffer
    [MCAN_MSG_BUF_SIZE_IN_WORDS];
#endif

/** @brief 最近收到的一条 MCAN 原始消息。 */
static volatile mcan_rx_message_t g_usb2can_last_rx_message;

/**
 * @brief 将 MCAN 驱动消息对象翻译为项目内部标准 CAN 帧。
 *
 * @param source 驱动层原始接收对象。
 * @param target 输出的项目内部 CAN 标准帧对象。
 */
static void usb2can_can_convert_rx_message(const mcan_rx_message_t* source,
                                           Usb2CanStandardFrame* target) {
  memset(target, 0, sizeof(*target));
  target->can_id = (uint16_t)(source->std_id & 0x07FFU);
  target->dlc = (uint8_t)source->dlc;
  if (target->dlc > USB2CAN_CAN_MAX_PAYLOAD_SIZE) {
    target->dlc = USB2CAN_CAN_MAX_PAYLOAD_SIZE;
  }
  memcpy(target->payload, source->data_8, target->dlc);
}

/**
 * @brief MCAN 中断服务函数。
 *
 * 当前中断处理保持极简：只读取 RXFIFO0 的新报文并立即回调上层桥接逻辑。
 *
 * @return 无返回值。
 */
SDK_DECLARE_EXT_ISR_M(BOARD_APP_CAN_IRQn, usb2can_can_isr)
void usb2can_can_isr(void) {
  uint32_t flags = mcan_get_interrupt_flags(BOARD_APP_CAN_BASE);
  uint32_t handled_flags = 0U;

  while ((flags & MCAN_INT_RXFIFO0_NEW_MSG) != 0U) {
    Usb2CanStandardFrame frame;

    (void)mcan_read_rxfifo(BOARD_APP_CAN_BASE, 0U,
                           (mcan_rx_message_t*)&g_usb2can_last_rx_message);
    usb2can_can_convert_rx_message(
        (const mcan_rx_message_t*)&g_usb2can_last_rx_message, &frame);
    if (g_usb2can_can_rx_callback != NULL) {
      g_usb2can_can_rx_callback(&frame);
    }
    handled_flags |= MCAN_INT_RXFIFO0_NEW_MSG;
    flags = mcan_get_interrupt_flags(BOARD_APP_CAN_BASE);
  }

  if (handled_flags != 0U) {
    mcan_clear_interrupt_flags(BOARD_APP_CAN_BASE, handled_flags);
  }
  if ((flags & ~handled_flags) != 0U) {
    mcan_clear_interrupt_flags(BOARD_APP_CAN_BASE, flags & ~handled_flags);
  }
}

/**
 * @brief 初始化 MCAN 外设与接收中断。
 *
 * @param rx_callback 收帧后的上报回调。
 * @return 初始化状态。
 */
Usb2CanStatus usb2can_can_init(const Usb2CanCanConfig* config,
                               Usb2CanCanRxCallback rx_callback) {
  mcan_config_t mcan_config;
  uint32_t can_clock = 0U;

  if (config == NULL) {
    return kUsb2CanStatusInvalidArgument;
  }

  g_usb2can_can_rx_callback = rx_callback;

  board_init_can(BOARD_APP_CAN_BASE);
  can_clock = board_init_can_clock(BOARD_APP_CAN_BASE);

#if defined(MCAN_SOC_MSG_BUF_IN_AHB_RAM) && (MCAN_SOC_MSG_BUF_IN_AHB_RAM == 1)
  {
    mcan_msg_buf_attr_t attr = {(uint32_t)&g_usb2can_can_msg_buffer,
                                sizeof(g_usb2can_can_msg_buffer)};
    if (mcan_set_msg_buf_attr(BOARD_APP_CAN_BASE, &attr) != status_success) {
      return kUsb2CanStatusIoError;
    }
  }
#endif

  mcan_get_default_config(BOARD_APP_CAN_BASE, &mcan_config);
  mcan_config.enable_canfd = false;
  mcan_config.baudrate = config->baudrate;
  mcan_config.interrupt_mask = MCAN_INT_RXFIFO0_NEW_MSG;
  mcan_config.txbuf_trans_interrupt_mask = 0U;
  mcan_config.txbuf_cancel_finish_interrupt_mask = 0U;

  if (mcan_init(BOARD_APP_CAN_BASE, &mcan_config, can_clock) !=
      status_success) {
    return kUsb2CanStatusIoError;
  }

  intc_m_enable_irq_with_priority(BOARD_APP_CAN_IRQn, 1);
  return kUsb2CanStatusOk;
}

/**
 * @brief 发送一条标准 CAN 数据帧。
 *
 * @param frame 待发送的 CAN 标准帧。
 * @return 发送状态。
 */
Usb2CanStatus usb2can_can_send(const Usb2CanStandardFrame* frame) {
  mcan_tx_frame_t tx_frame;

  if (frame == NULL || frame->can_id > 0x07FFU ||
      frame->dlc > USB2CAN_CAN_MAX_PAYLOAD_SIZE) {
    return kUsb2CanStatusInvalidArgument;
  }

  memset(&tx_frame, 0, sizeof(tx_frame));
  tx_frame.std_id = frame->can_id;
  tx_frame.use_ext_id = 0U;
  tx_frame.canfd_frame = 0U;
  tx_frame.bitrate_switch = 0U;
  tx_frame.dlc = frame->dlc;
  memcpy(tx_frame.data_8, frame->payload, frame->dlc);

  if (mcan_transmit_blocking(BOARD_APP_CAN_BASE, &tx_frame) != status_success) {
    return kUsb2CanStatusIoError;
  }

  return kUsb2CanStatusOk;
}
