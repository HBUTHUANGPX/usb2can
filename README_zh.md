# USB2CAN 主机协议说明

## 概述

`usb2can` 工程实现了一个最小桥接网关：

- 主机通过 USB CDC ACM 虚拟串口向 MCU 发送自定义协议包
- MCU 收到合法协议包后，立即转换为 1 条 CAN2.0 标准数据帧发送
- MCU 收到 1 条 CAN2.0 标准数据帧后，立即封装为 1 个协议包回传主机

当前版本范围固定如下：

- 只支持 `CAN2.0 标准帧`
- 只支持 `11-bit CAN ID`
- 只支持 `DLC <= 8`
- 当前 `1 个 USB 包 <-> 1 个 CAN 帧`
- 不承载电机业务语义，只做协议转换

## 集中配置项

项目内与协议和链路相关的集中配置位于 [usb2can_config.h](/home/hpx/HPXLoco_5/hpm_work/usb2can/inc/usb2can_config.h)。

当前默认值包括：

- 协议帧头：`0xA5`
- CAN 波特率：`1000000 bps`
- USB 总线编号：`0`
- CDC IN 端点：`0x81`
- CDC OUT 端点：`0x01`
- CDC INT 端点：`0x83`
- USB 产品字符串：`USB2CAN Bridge`

后续若要切换协议帧头、CAN 波特率、CDC 缓冲区大小或 USB 字符串，直接修改该头文件即可。

## 线协议格式

USB 线上一条完整协议帧的字节布局如下：

```text
+--------+-------+--------+------+--------+-----------+
| head   | cmd   | len    | crc8 | crc16  | data[len] |
| 1 byte | 1 byte| 2 bytes|1 byte|2 bytes | len bytes |
+--------+-------+--------+------+--------+-----------+
```

字段说明：

- `head`
  协议帧头，当前默认值为 `0xA5`
- `cmd`
  命令字，标识该包的方向与用途
- `len`
  `data[]` 的长度，不包含 `crc16`
- `crc8`
  对前 4 字节 `head + cmd + len` 做 CRC8 校验
- `crc16`
  对后续 `data[len]` 做 CRC16 校验
- `data[len]`
  业务负载。当前工程里该负载直接描述一条标准 CAN 帧

### 字节序

协议中的 16-bit 字段按小端序编码：

- `len` 为小端
- `crc16` 为小端
- `data[]` 中的 `can_id` 为小端

## 命令字定义

命令字定义位于 [usb2can_types.h](/home/hpx/HPXLoco_5/hpm_work/usb2can/inc/usb2can_types.h)。

当前版本支持以下 `cmd`：

| cmd 值 | 名称 | 方向 | 含义 |
|---|---|---|---|
| `0x01` | `CMD_CAN_TX` | Host -> Device | 主机请求 MCU 发送 1 条标准 CAN 数据帧 |
| `0x02` | `CMD_CAN_RX_REPORT` | Device -> Host | MCU 上报刚收到的 1 条标准 CAN 数据帧 |
| `0x7F` | `CMD_ERROR_REPORT` | Device -> Host | MCU 上报解析、校验或底层收发错误 |

## `data[]` 负载格式

当前所有 CAN 相关命令都使用同一种变长负载：

```text
+------------+------+----------------+
| can_id     | dlc  | payload[dlc]   |
| 2 bytes LE |1 byte| 0~8 bytes       |
+------------+------+----------------+
```

字段约束：

- `can_id`
  仅低 11 位有效，取值范围 `0x000 ~ 0x7FF`
- `dlc`
  取值范围 `0 ~ 8`
- `payload`
  实际长度等于 `dlc`

因此：

- `len = 3 + dlc`
- 最短负载长度为 `3`
- 最长负载长度为 `11`

### 发送 CAN 的主机请求示例

如果主机想发送一条 CAN 帧：

- `can_id = 0x123`
- `dlc = 4`
- `payload = 11 22 33 44`

则 `data[]` 应编码为：

```text
23 01 04 11 22 33 44
```

说明：

- `0x123` 以小端表示为 `0x23 0x01`
- `dlc = 0x04`
- 后面紧跟 4 字节数据

## 错误码定义

错误码定义位于 [usb2can_types.h](/home/hpx/HPXLoco_5/hpm_work/usb2can/inc/usb2can_types.h) 中的 `Usb2CanErrorCode`。

当设备发送 `CMD_ERROR_REPORT` 时：

- `len` 固定为 `1`
- `data[0]` 为单字节错误码

当前错误码表如下：

| 错误码 | 名称 | 含义 |
|---|---|---|
| `0x00` | `None` | 无错误，通常不会主动上报 |
| `0x01` | `InvalidArgument` | 参数非法，例如 `can_id > 0x7FF` |
| `0x02` | `BufferTooSmall` | 发送或解析缓冲区空间不足 |
| `0x03` | `ChecksumError` | CRC8 或 CRC16 校验失败 |
| `0x04` | `LengthError` | `len`、`dlc` 或真实数据长度不匹配 |
| `0x05` | `NeedMoreData` | 当前分片还不足以拼出完整协议包 |
| `0x06` | `Unsupported` | 不支持的帧头或命令字 |
| `0x07` | `IoError` | USB 或 CAN 底层收发失败 |

说明：

- `NeedMoreData` 一般是内部解析状态，不一定总会上报给主机
- `IoError` 表示底层硬件或驱动动作失败，例如 CAN 发送接口返回失败

## 主机对接建议

主机侧建议按如下方式实现：

1. 将串口视为二进制通道，而不是文本行协议
2. 发送前先按小端构造 `len`、`can_id` 和 `crc16`
3. 接收时先查找 `head`
4. 收齐固定头部后读取 `len`
5. 再继续接收 `crc16 + data[len]`
6. 先验 `crc8`，再验 `crc16`
7. 根据 `cmd` 分发到 CAN 上报处理或错误处理逻辑

## 当前实现文件

- 顶层编排：[usb2can_app.c](/home/hpx/HPXLoco_5/hpm_work/usb2can/src/usb2can_app.c)
- 协议层：[usb2can_protocol.c](/home/hpx/HPXLoco_5/hpm_work/usb2can/src/usb2can_protocol.c)
- 桥接层：[usb2can_bridge.c](/home/hpx/HPXLoco_5/hpm_work/usb2can/src/usb2can_bridge.c)
- CAN 适配层：[usb2can_can.c](/home/hpx/HPXLoco_5/hpm_work/usb2can/src/usb2can_can.c)
- USB CDC 适配层：[cdc_acm.c](/home/hpx/HPXLoco_5/hpm_work/usb2can/src/cdc_acm.c)
