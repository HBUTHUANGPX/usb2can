/**
 * @file usb2can_can.c
 * @brief USB2CAN 的 MCAN 适配层实现。
 */

#include "usb2can_can.h"

#include <string.h>

#include "board.h"
#include "hpm_mcan_drv.h"
#include "usb2can_bridge.h"

/** @brief 当前注册的 CAN 接收回调。 */
static Usb2CanCanRxCallback g_usb2can_can_rx_callback = NULL;
/** @brief 当前 CAN 适配层配置。 */
static Usb2CanCanConfig g_usb2can_can_config = {0};
/** @brief 当前活动模式。 */
static Usb2CanMode g_usb2can_can_mode = kUsb2CanModeCan2Std;
/** @brief RXFIFO0 满计数。 */
static volatile uint32_t g_usb2can_can_rxfifo0_full_count = 0U;
/** @brief RXFIFO0 丢帧计数。 */
static volatile uint32_t g_usb2can_can_rxfifo0_lost_count = 0U;
/** @brief RXFIFO1 满计数。 */
static volatile uint32_t g_usb2can_can_rxfifo1_full_count = 0U;
/** @brief RXFIFO1 丢帧计数。 */
static volatile uint32_t g_usb2can_can_rxfifo1_lost_count = 0U;

#define USB2CAN_CAN_RXFIFO0_FLAGS \
  (MCAN_INT_RXFIFO0_NEW_MSG | MCAN_INT_RXFIFO0_FULL | \
   MCAN_INT_RXFIFO0_MSG_LOST | MCAN_INT_RXFIFO0_WMK_REACHED)

#define USB2CAN_CAN_RXFIFO1_FLAGS \
  (MCAN_INT_RXFIFO1_NEW_MSG | MCAN_INT_RXFIFO1_FULL | \
   MCAN_INT_RXFIFO1_MSG_LOST | MCAN_INT_RXFIFO1_WMK_REACHED)

#define USB2CAN_CAN_RX_REARM_FLAGS \
  (USB2CAN_CAN_RXFIFO0_FLAGS | USB2CAN_CAN_RXFIFO1_FLAGS | \
   MCAN_INT_MSG_STORE_TO_RXBUF)

#define USB2CAN_CAN_KNOWN_ISR_FLAGS \
  (USB2CAN_CAN_RX_REARM_FLAGS | MCAN_INT_TXFIFO_EMPTY)

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
 * @brief 打印 MCAN 发送失败时的协议状态与错误计数。
 *
 * SDK 的 blocking 发送失败常见原因是等待 TX 完成超时；现场状态能区分 ACK、
 * 数据相位错误、error passive/bus-off 等总线侧问题。
 */
static void usb2can_can_log_tx_failure(hpm_stat_t status) {
  mcan_error_count_t error_count = {0};
  mcan_protocol_status_t protocol_status = {0};
  uint32_t interrupt_flags = mcan_get_interrupt_flags(BOARD_APP_CAN_BASE);

  mcan_get_error_counter(BOARD_APP_CAN_BASE, &error_count);
  (void)mcan_get_protocol_status(BOARD_APP_CAN_BASE, &protocol_status);

  printf("[usb2can][can] tx fail status=%d ir=0x%08lX lec=%u dlec=%u "
         "act=%u tec=%u rec=%u cel=%u warn=%u passive=%u busoff=%u "
         "pxe=%u tdcv=%u\n",
         (int)status, (unsigned long)interrupt_flags,
         (unsigned int)mcan_get_last_error_code(BOARD_APP_CAN_BASE),
         (unsigned int)mcan_get_last_data_error_code(BOARD_APP_CAN_BASE),
         (unsigned int)protocol_status.activity,
         (unsigned int)error_count.transmit_error_count,
         (unsigned int)error_count.receive_error_count,
         (unsigned int)error_count.can_error_logging_count,
         protocol_status.in_warning_state ? 1U : 0U,
         protocol_status.in_error_passive_state ? 1U : 0U,
         protocol_status.in_bus_off_state ? 1U : 0U,
         protocol_status.protocol_exception_evt_occurred ? 1U : 0U,
         (unsigned int)protocol_status.tdc_val);
}

/**
 * @brief 将 MCAN 驱动消息对象翻译为项目内部 CAN 帧。
 *
 * @param source 驱动层原始接收对象。
 * @param target 输出的项目内部 CAN 标准帧对象。
 */
static void usb2can_can_convert_rx_message(const mcan_rx_message_t* source,
                                           Usb2CanBusFrame* target) {
  uint8_t data_length = 0U;

  memset(target, 0, sizeof(*target));
  target->is_extended_id = (source->use_ext_id != 0U);
  target->can_id = target->is_extended_id ? (source->ext_id & 0x1FFFFFFFUL)
                                          : (source->std_id & 0x07FFU);

  if (source->canfd_frame != 0U) {
    if (usb2can_bridge_canfd_dlc_to_length((uint8_t)source->dlc,
                                           &data_length) != kUsb2CanStatusOk) {
      data_length = 0U;
    }
    target->mode = (source->bitrate_switch != 0U)
                       ? kUsb2CanModeCanFdStdBrs
                       : kUsb2CanModeCanFdStd;
    target->data_length = data_length;
  } else {
    target->mode = kUsb2CanModeCan2Std;
    target->data_length = (uint8_t)source->dlc;
    if (target->data_length > USB2CAN_CAN_MAX_PAYLOAD_SIZE) {
      target->data_length = USB2CAN_CAN_MAX_PAYLOAD_SIZE;
    }
  }

  memcpy(target->payload, source->data_8, target->data_length);
}

static uint32_t usb2can_can_get_rxfifo_fill_level(uint32_t fifo_index) {
  if (fifo_index == 0U) {
    return MCAN_RXF0S_F0FL_GET(BOARD_APP_CAN_BASE->RXF0S);
  }
  return MCAN_RXF1S_F1FL_GET(BOARD_APP_CAN_BASE->RXF1S);
}

static uint32_t usb2can_can_drain_rxfifo(uint32_t fifo_index,
                                         bool forward_to_usb) {
  uint32_t drained_count = 0U;

  while (usb2can_can_get_rxfifo_fill_level(fifo_index) > 0U) {
    Usb2CanBusFrame frame;

    if (mcan_read_rxfifo(BOARD_APP_CAN_BASE, fifo_index,
                         (mcan_rx_message_t*)&g_usb2can_last_rx_message) !=
        status_success) {
      printf("[usb2can][can] read rxfifo%lu failed\n",
             (unsigned long)fifo_index);
      break;
    }
    if (forward_to_usb) {
      usb2can_can_convert_rx_message(
          (const mcan_rx_message_t*)&g_usb2can_last_rx_message, &frame);
      if (g_usb2can_can_rx_callback != NULL) {
        g_usb2can_can_rx_callback(&frame);
      } else {
        printf("[usb2can][can] rx callback is null\n");
      }
    }
    drained_count++;
  }

  return drained_count;
}

static void usb2can_can_rearm_rx_path(const char* reason) {
  const uint32_t before_flags = mcan_get_interrupt_flags(BOARD_APP_CAN_BASE);
  const uint32_t before_fifo0 =
      usb2can_can_get_rxfifo_fill_level(0U);
  const uint32_t before_fifo1 =
      usb2can_can_get_rxfifo_fill_level(1U);
  const uint32_t drained_fifo0 = usb2can_can_drain_rxfifo(0U, false);
  const uint32_t drained_fifo1 = usb2can_can_drain_rxfifo(1U, false);
  const uint32_t clear_flags = mcan_get_interrupt_flags(BOARD_APP_CAN_BASE);

  if (clear_flags != 0U) {
    mcan_clear_interrupt_flags(BOARD_APP_CAN_BASE, clear_flags);
  }

  if (before_flags != 0U || before_fifo0 != 0U || before_fifo1 != 0U ||
      drained_fifo0 != 0U || drained_fifo1 != 0U || clear_flags != 0U) {
    printf("[usb2can][can] rx path rearmed reason=%s flags_before=0x%08lX "
           "flags_cleared=0x%08lX fifo0_before=%lu fifo1_before=%lu "
           "drained0=%lu drained1=%lu\n",
           reason != NULL ? reason : "unknown",
           (unsigned long)before_flags, (unsigned long)clear_flags,
           (unsigned long)before_fifo0, (unsigned long)before_fifo1,
           (unsigned long)drained_fifo0, (unsigned long)drained_fifo1);
  }
}

/**
 * @brief 将 CAN FD 消息 RAM 调整为接收优先布局。
 *
 * HPM SDK 的 CAN FD 默认 RXFIFO0 深度只有 8。USB2CAN 的 CAN->USB 压测会
 * 遇到外部分析仪无间隔 burst，优先扩大 RXFIFO0，并关闭当前未使用的
 * RXFIFO1/RXBUF，保留默认 TX buffer 与 TX event FIFO。
 *
 * @param ram_config 待调整的 MCAN RAM 配置。
 */
static void usb2can_can_prepare_canfd_rx_ram_config(
    mcan_ram_config_t* ram_config) {
  if (ram_config == NULL) {
    return;
  }

  ram_config->rxfifos[0].enable = 1U;
  ram_config->rxfifos[0].elem_count =
      USB2CAN_CONFIG_CANFD_RXFIFO0_ELEM_COUNT;
  ram_config->rxfifos[0].watermark = 1U;
  ram_config->rxfifos[0].operation_mode = MCAN_FIFO_OPERATION_MODE_BLOCKING;
  ram_config->rxfifos[0].data_field_size = MCAN_DATA_FIELD_SIZE_64BYTES;

  ram_config->rxfifos[1].enable = 0U;
  ram_config->rxfifos[1].elem_count = 0U;
  ram_config->rxfifos[1].watermark = 0U;
  ram_config->rxfifos[1].operation_mode = MCAN_FIFO_OPERATION_MODE_BLOCKING;
  ram_config->rxfifos[1].data_field_size = 0U;

  ram_config->enable_rxbuf = false;
  ram_config->rxbuf_elem_count = 0U;
  ram_config->rxbuf_data_field_size = 0U;

  ram_config->txbuf_dedicated_txbuf_elem_count = 0U;
  ram_config->txbuf_fifo_or_queue_elem_count =
      USB2CAN_CONFIG_CANFD_TXFIFO_ELEM_COUNT;

  ram_config->enable_tx_evt_fifo = false;
  ram_config->tx_evt_fifo_elem_count = 0U;
  ram_config->tx_evt_fifo_watermark = 0U;
}

/**
 * @brief 根据模式准备一份 MCAN 初始化配置。
 *
 * @param mode 目标模式。
 * @param can_clock CAN 时钟频率。
 * @param mcan_config 输出的 MCAN 配置。
 * @return 操作状态。
 */
static Usb2CanStatus usb2can_can_prepare_mcan_config(Usb2CanMode mode,
                                                     uint32_t can_clock,
                                                     mcan_config_t* mcan_config) {
  if (mcan_config == NULL || can_clock == 0U) {
    return kUsb2CanStatusInvalidArgument;
  }

  mcan_get_default_config(BOARD_APP_CAN_BASE, mcan_config);
  mcan_config->baudrate = g_usb2can_can_config.baudrate;
  mcan_config->can20_samplepoint_min =
      g_usb2can_can_config.samplepoint_per_mille;
  mcan_config->can20_samplepoint_max =
      g_usb2can_can_config.samplepoint_per_mille;
  mcan_config->interrupt_mask =
      MCAN_INT_RXFIFO0_NEW_MSG | MCAN_INT_RXFIFO0_FULL |
      MCAN_INT_RXFIFO0_MSG_LOST | MCAN_INT_RXFIFO0_WMK_REACHED |
      MCAN_INT_RXFIFO1_NEW_MSG | MCAN_INT_RXFIFO1_FULL |
      MCAN_INT_RXFIFO1_MSG_LOST | MCAN_INT_RXFIFO1_WMK_REACHED;
  mcan_config->txbuf_trans_interrupt_mask = 0U;
  mcan_config->txbuf_cancel_finish_interrupt_mask = 0U;
  mcan_config->enable_canfd = false;

  if (mode == kUsb2CanModeCanFdStd || mode == kUsb2CanModeCanFdStdBrs) {
    mcan_config->enable_canfd = true;
    mcan_config->enable_tdc = g_usb2can_can_config.enable_tdc;
    mcan_config->tdc_config.ssp_offset =
        g_usb2can_can_config.tdc_ssp_offset;
    mcan_config->tdc_config.filter_window_length =
        g_usb2can_can_config.tdc_filter_window;
    mcan_config->baudrate_fd = g_usb2can_can_config.baudrate_fd;
    mcan_config->canfd_samplepoint_min =
        g_usb2can_can_config.samplepoint_fd_per_mille;
    mcan_config->canfd_samplepoint_max =
        g_usb2can_can_config.samplepoint_fd_per_mille;
    mcan_get_default_ram_config(BOARD_APP_CAN_BASE, &mcan_config->ram_config,
                                true);
    usb2can_can_prepare_canfd_rx_ram_config(&mcan_config->ram_config);
  }

  return kUsb2CanStatusOk;
}

/**
 * @brief 重新初始化 MCAN 到目标模式。
 *
 * @param mode 目标模式。
 * @return 操作状态。
 */
static Usb2CanStatus usb2can_can_apply_mode(Usb2CanMode mode) {
  mcan_config_t mcan_config;
  uint32_t can_clock = 0U;

  board_init_can(BOARD_APP_CAN_BASE);
  can_clock = board_init_can_clock(BOARD_APP_CAN_BASE);
  if (usb2can_can_prepare_mcan_config(mode, can_clock, &mcan_config) !=
      kUsb2CanStatusOk) {
    return kUsb2CanStatusInvalidArgument;
  }

  intc_m_disable_irq(BOARD_APP_CAN_IRQn);
  mcan_deinit(BOARD_APP_CAN_BASE);
  if (mcan_init(BOARD_APP_CAN_BASE, &mcan_config, can_clock) != status_success) {
    printf("[usb2can][can] mcan_init failed mode=%u baud=%lu sp=%u "
           "baud_fd=%lu sp_fd=%u\n",
           (unsigned int)mode, (unsigned long)mcan_config.baudrate,
           (unsigned int)mcan_config.can20_samplepoint_min,
           (unsigned long)mcan_config.baudrate_fd,
           (unsigned int)mcan_config.canfd_samplepoint_min);
    return kUsb2CanStatusIoError;
  }

  intc_m_enable_irq_with_priority(BOARD_APP_CAN_IRQn, 1);
  g_usb2can_can_mode = mode;
  printf("[usb2can][can] active mode=%u clock=%lu baud=%lu sp=%u "
         "baud_fd=%lu sp_fd=%u canfd=%d tdc=%d tdco_cfg=%u tdcf_cfg=%u "
         "rxfifo0=%u rxfifo1=%u rxbuf=%u txfifo=%u dbtp=0x%08lX "
         "tdcr=0x%08lX\n",
         (unsigned int)mode, (unsigned long)can_clock,
         (unsigned long)mcan_config.baudrate,
         (unsigned int)mcan_config.can20_samplepoint_min,
         (unsigned long)mcan_config.baudrate_fd,
         (unsigned int)mcan_config.canfd_samplepoint_min,
         mcan_config.enable_canfd ? 1 : 0, mcan_config.enable_tdc ? 1 : 0,
         (unsigned int)mcan_config.tdc_config.ssp_offset,
         (unsigned int)mcan_config.tdc_config.filter_window_length,
         (unsigned int)mcan_config.ram_config.rxfifos[0].elem_count,
         mcan_config.ram_config.rxfifos[1].enable
             ? (unsigned int)mcan_config.ram_config.rxfifos[1].elem_count
             : 0U,
         mcan_config.ram_config.enable_rxbuf
             ? (unsigned int)mcan_config.ram_config.rxbuf_elem_count
             : 0U,
         (unsigned int)mcan_config.ram_config.txbuf_fifo_or_queue_elem_count,
         (unsigned long)BOARD_APP_CAN_BASE->DBTP,
         (unsigned long)BOARD_APP_CAN_BASE->TDCR);
  return kUsb2CanStatusOk;
}

/**
 * @brief MCAN 中断服务函数。
 *
 * 当前中断处理保持极简：读取 RXFIFO0/RXFIFO1 的新报文并立即回调上层桥接逻辑。
 *
 * @return 无返回值。
 */
SDK_DECLARE_EXT_ISR_M(BOARD_APP_CAN_IRQn, usb2can_can_isr)
void usb2can_can_isr(void) {
  uint32_t flags = mcan_get_interrupt_flags(BOARD_APP_CAN_BASE);

  if (flags == 0U) {
    return;
  }

  /*
   * Clear the interrupt snapshot before draining RX FIFO. If we clear this
   * snapshot at ISR exit, an RF0N generated while the ISR is draining can be
   * cleared accidentally, leaving later frames stuck until FIFO full/lost.
   */
  mcan_clear_interrupt_flags(BOARD_APP_CAN_BASE, flags);

  if ((flags & MCAN_INT_RXFIFO0_FULL) != 0U) {
    g_usb2can_can_rxfifo0_full_count++;
    printf("[usb2can][can-isr] rxfifo0 full count=%lu\n",
           (unsigned long)g_usb2can_can_rxfifo0_full_count);
  }
  if ((flags & MCAN_INT_RXFIFO0_MSG_LOST) != 0U) {
    g_usb2can_can_rxfifo0_lost_count++;
    printf("[usb2can][can-isr] rxfifo0 lost count=%lu\n",
           (unsigned long)g_usb2can_can_rxfifo0_lost_count);
  }
  if ((flags & USB2CAN_CAN_RXFIFO0_FLAGS) != 0U) {
    (void)usb2can_can_drain_rxfifo(0U, true);
  }

  if ((flags & MCAN_INT_RXFIFO1_FULL) != 0U) {
    g_usb2can_can_rxfifo1_full_count++;
    printf("[usb2can][can-isr] rxfifo1 full count=%lu\n",
           (unsigned long)g_usb2can_can_rxfifo1_full_count);
  }
  if ((flags & MCAN_INT_RXFIFO1_MSG_LOST) != 0U) {
    g_usb2can_can_rxfifo1_lost_count++;
    printf("[usb2can][can-isr] rxfifo1 lost count=%lu\n",
           (unsigned long)g_usb2can_can_rxfifo1_lost_count);
  }
  if ((flags & USB2CAN_CAN_RXFIFO1_FLAGS) != 0U) {
    (void)usb2can_can_drain_rxfifo(1U, true);
  }
  if ((flags & MCAN_INT_MSG_STORE_TO_RXBUF) != 0U) {
    printf("[usb2can][can-isr] rx buffer store flag set flags=0x%08lX\n",
           (unsigned long)flags);
  }
  if ((flags & ~USB2CAN_CAN_KNOWN_ISR_FLAGS) != 0U) {
    printf("[usb2can][can-isr] other flags=0x%08lX\n",
           (unsigned long)(flags & ~USB2CAN_CAN_KNOWN_ISR_FLAGS));
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
  if (config == NULL) {
    return kUsb2CanStatusInvalidArgument;
  }

  g_usb2can_can_rx_callback = rx_callback;
  g_usb2can_can_config = *config;
  g_usb2can_can_mode = config->initial_mode;

  printf("[usb2can][can] init requested mode=%u baud=%lu sp=%u "
         "baud_fd=%lu sp_fd=%u tdc=%u tdco=%u tdcf=%u\n",
         (unsigned int)config->initial_mode, (unsigned long)config->baudrate,
         (unsigned int)config->samplepoint_per_mille,
         (unsigned long)config->baudrate_fd,
         (unsigned int)config->samplepoint_fd_per_mille,
         config->enable_tdc ? 1U : 0U,
         (unsigned int)config->tdc_ssp_offset,
         (unsigned int)config->tdc_filter_window);

#if defined(MCAN_SOC_MSG_BUF_IN_AHB_RAM) && (MCAN_SOC_MSG_BUF_IN_AHB_RAM == 1)
  {
    mcan_msg_buf_attr_t attr = {(uint32_t)&g_usb2can_can_msg_buffer,
                                sizeof(g_usb2can_can_msg_buffer)};
    if (mcan_set_msg_buf_attr(BOARD_APP_CAN_BASE, &attr) != status_success) {
      return kUsb2CanStatusIoError;
    }
  }
#endif
  return usb2can_can_apply_mode(config->initial_mode);
}

Usb2CanMode usb2can_can_get_mode(void) {
  return g_usb2can_can_mode;
}

Usb2CanStatus usb2can_can_reconfigure(Usb2CanMode mode) {
  if (mode != kUsb2CanModeCan2Std && mode != kUsb2CanModeCanFdStd &&
      mode != kUsb2CanModeCanFdStdBrs) {
    return kUsb2CanStatusInvalidArgument;
  }

  if (mode == g_usb2can_can_mode) {
    if (mcan_is_in_busoff_state(BOARD_APP_CAN_BASE)) {
      printf("[usb2can][can] reconfigure recovering bus-off mode=%u\n",
             (unsigned int)mode);
      return usb2can_can_apply_mode(mode);
    }
    usb2can_can_rearm_rx_path("same-mode");
    printf("[usb2can][can] reconfigure skipped mode=%u unchanged rx_rearmed=1\n",
           (unsigned int)mode);
    return kUsb2CanStatusOk;
  }

  printf("[usb2can][can] reconfigure begin old=%u new=%u\n",
         (unsigned int)g_usb2can_can_mode, (unsigned int)mode);
  return usb2can_can_apply_mode(mode);
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

  hpm_stat_t status = mcan_transmit_blocking(BOARD_APP_CAN_BASE, &tx_frame);
  if (status != status_success) {
    printf("[usb2can][can] mcan_transmit_blocking failed\n");
    usb2can_can_log_tx_failure(status);
    return kUsb2CanStatusIoError;
  }

  return kUsb2CanStatusOk;
}

Usb2CanStatus usb2can_can_send_fd(const Usb2CanFdStandardFrame* frame,
                                  bool enable_brs) {
  mcan_tx_frame_t tx_frame;
  uint8_t dlc = 0U;

  if (frame == NULL || frame->can_id > 0x07FFU) {
    return kUsb2CanStatusInvalidArgument;
  }
  if (usb2can_bridge_canfd_length_to_dlc(frame->data_length, &dlc) !=
      kUsb2CanStatusOk) {
    return kUsb2CanStatusInvalidArgument;
  }

  memset(&tx_frame, 0, sizeof(tx_frame));
  tx_frame.std_id = frame->can_id;
  tx_frame.use_ext_id = 0U;
  tx_frame.canfd_frame = 1U;
  tx_frame.bitrate_switch = enable_brs ? 1U : 0U;
  tx_frame.dlc = dlc;
  memcpy(tx_frame.data_8, frame->payload, frame->data_length);

  hpm_stat_t status = mcan_transmit_blocking(BOARD_APP_CAN_BASE, &tx_frame);
  if (status != status_success) {
    printf("[usb2can][can] mcan_transmit_blocking fd failed id=0x%03X len=%u brs=%d\n",
           frame->can_id, frame->data_length, enable_brs ? 1 : 0);
    usb2can_can_log_tx_failure(status);
    return kUsb2CanStatusIoError;
  }

  return kUsb2CanStatusOk;
}

Usb2CanStatus usb2can_can_send_fd_ext(const Usb2CanFdExtendedFrame* frame,
                                      bool enable_brs) {
  mcan_tx_frame_t tx_frame;
  uint8_t dlc = 0U;

  if (frame == NULL || frame->can_id > 0x1FFFFFFFUL) {
    return kUsb2CanStatusInvalidArgument;
  }
  if (usb2can_bridge_canfd_length_to_dlc(frame->data_length, &dlc) !=
      kUsb2CanStatusOk) {
    return kUsb2CanStatusInvalidArgument;
  }

  memset(&tx_frame, 0, sizeof(tx_frame));
  tx_frame.ext_id = frame->can_id;
  tx_frame.use_ext_id = 1U;
  tx_frame.canfd_frame = 1U;
  tx_frame.bitrate_switch = enable_brs ? 1U : 0U;
  tx_frame.dlc = dlc;
  memcpy(tx_frame.data_8, frame->payload, frame->data_length);

  hpm_stat_t status = mcan_transmit_blocking(BOARD_APP_CAN_BASE, &tx_frame);
  if (status != status_success) {
    printf("[usb2can][can] mcan_transmit_blocking fd ext failed id=0x%08lX len=%u brs=%d\n",
           (unsigned long)frame->can_id, frame->data_length,
           enable_brs ? 1 : 0);
    usb2can_can_log_tx_failure(status);
    return kUsb2CanStatusIoError;
  }

  return kUsb2CanStatusOk;
}
