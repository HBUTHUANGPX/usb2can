/**
 * @file usb2can_types.h
 * @brief USB2CAN 工程公共类型定义。
 */

#ifndef USB2CAN_INC_USB2CAN_TYPES_H_
#define USB2CAN_INC_USB2CAN_TYPES_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** @brief USB2CAN 支持的 CAN 数据区最大字节数。 */
#define USB2CAN_CAN_MAX_PAYLOAD_SIZE 8U

/** @brief USB2CAN 自定义协议头部固定字节数，不包含 crc16 与 data。 */
#define USB2CAN_PROTOCOL_HEADER_SIZE 5U

/** @brief USB2CAN 自定义协议中 crc16 字段字节数。 */
#define USB2CAN_PROTOCOL_CRC16_SIZE 2U

/**
 * @brief 表示通用执行状态的返回值枚举。
 *
 * 该枚举被协议层、桥接层与驱动封装层共享，用于统一表达成功、输入非法、
 * 缓冲区不足、校验失败等常见状态，方便上层据此决定是否上报错误或继续重试。
 */
typedef enum Usb2CanStatus {
  /** @brief 操作成功完成。 */
  kUsb2CanStatusOk = 0,
  /** @brief 输入参数为空或格式非法。 */
  kUsb2CanStatusInvalidArgument = 1,
  /** @brief 缓冲区空间不足，无法继续写入或保存数据。 */
  kUsb2CanStatusBufferTooSmall = 2,
  /** @brief 协议校验失败，通常代表 CRC 或长度不匹配。 */
  kUsb2CanStatusChecksumError = 3,
  /** @brief 数据长度不符合协议或 CAN 约束。 */
  kUsb2CanStatusLengthError = 4,
  /** @brief 解析尚未完成，需要更多输入字节。 */
  kUsb2CanStatusNeedMoreData = 5,
  /** @brief 接收到的内容无法识别或不支持。 */
  kUsb2CanStatusUnsupported = 6,
  /** @brief 底层发送、初始化或驱动动作失败。 */
  kUsb2CanStatusIoError = 7,
} Usb2CanStatus;

/**
 * @brief 描述 USB2CAN 支持的协议命令字。
 *
 * 当前版本只保留最小命令集：主机下发 CAN 帧、设备上报 CAN 帧、设备上报错误。
 * 后续若扩展调试信息或版本查询命令，可继续在此枚举中增加成员。
 */
typedef enum Usb2CanCommand {
  /** @brief 主机请求 MCU 发送一条 CAN 标准帧。 */
  kUsb2CanCommandCanTx = 0x01,
  /** @brief MCU 将收到的一条 CAN 标准帧上报给主机。 */
  kUsb2CanCommandCanRxReport = 0x02,
  /** @brief MCU 上报协议解析、校验或驱动层错误。 */
  kUsb2CanCommandErrorReport = 0x7F,
} Usb2CanCommand;

/**
 * @brief USB2CAN 主机可见错误码定义。
 *
 * 该枚举是 `CMD_ERROR_REPORT` 负载中的单字节错误码，对主机侧是稳定接口。
 * 当前错误码与内部状态一一映射，便于快速定位是参数错误、CRC 错误还是底层
 * 驱动发送失败。
 */
typedef enum Usb2CanErrorCode {
  /** @brief 操作成功，通常不会单独上报。 */
  kUsb2CanErrorCodeNone = 0x00,
  /** @brief 输入参数为空、格式错误或 ID 越界。 */
  kUsb2CanErrorCodeInvalidArgument = 0x01,
  /** @brief 输出或缓存空间不足。 */
  kUsb2CanErrorCodeBufferTooSmall = 0x02,
  /** @brief CRC8 或 CRC16 校验失败。 */
  kUsb2CanErrorCodeChecksumError = 0x03,
  /** @brief 长度字段与真实数据不匹配，或 DLC 超出范围。 */
  kUsb2CanErrorCodeLengthError = 0x04,
  /** @brief 当前分片尚未收齐完整协议包。 */
  kUsb2CanErrorCodeNeedMoreData = 0x05,
  /** @brief 命令字或帧头暂不支持。 */
  kUsb2CanErrorCodeUnsupported = 0x06,
  /** @brief USB 或 CAN 底层收发失败。 */
  kUsb2CanErrorCodeIoError = 0x07,
} Usb2CanErrorCode;

/**
 * @brief 表示一条 CAN2.0 标准数据帧。
 *
 * 该结构只覆盖当前项目范围内需要处理的内容：11-bit 标准 ID、DLC 与最多 8 字节
 * 的数据区，不包含扩展帧、远程帧和 CAN FD 等超出本期范围的字段。
 */
typedef struct Usb2CanStandardFrame {
  /** @brief 11-bit 标准帧 ID，仅低 11 位有效。 */
  uint16_t can_id;
  /** @brief 数据长度码，取值范围 0~8。 */
  uint8_t dlc;
  /** @brief CAN 数据区，实际使用前 dlc 个字节。 */
  uint8_t payload[USB2CAN_CAN_MAX_PAYLOAD_SIZE];
} Usb2CanStandardFrame;

/**
 * @brief 表示一条完整的 USB2CAN 协议包视图。
 *
 * 该结构用于协议打包与解包之间传递结果。为避免额外动态内存分配，`data` 指针
 * 始终由调用方提供的缓冲区承载，协议层只负责填写长度与字段，不负责管理所有权。
 */
typedef struct Usb2CanPacket {
  /** @brief 协议帧头。 */
  uint8_t head;
  /** @brief 协议命令字。 */
  uint8_t cmd;
  /** @brief data[] 的长度，不包含 crc16。 */
  uint16_t len;
  /** @brief 头部字段校验值。 */
  uint8_t crc8;
  /** @brief 数据区校验值。 */
  uint16_t crc16;
  /** @brief 指向调用方提供的数据缓冲区。 */
  uint8_t* data;
  /** @brief `data` 缓冲区总容量，用于解包时做越界保护。 */
  uint16_t data_capacity;
} Usb2CanPacket;

#endif  // USB2CAN_INC_USB2CAN_TYPES_H_
