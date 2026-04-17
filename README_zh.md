# USB2CAN 主机协议说明

## 概述

`usb2can` 工程实现了一个基于 USB CDC ACM 的最小桥接网关：

- 主机通过 USB CDC ACM 虚拟串口向 MCU 发送私有二进制协议包
- MCU 根据当前活动模式解析数据命令，并转发到 CAN 总线
- MCU 收到 CAN 总线报文后，按当前活动模式封装协议包回传主机

当前版本支持以下运行模式：

- `CAN2_STD`
- `CANFD_STD`
- `CANFD_STD_BRS`

当前版本范围固定如下：

- 只支持 `11-bit CAN ID`
- 只支持标准帧，不支持扩展帧
- 不承载电机业务语义，只做协议转换
- `1 个 USB 协议包 <-> 1 条 CAN/CAN FD 帧`

## 集中配置项

与协议和链路相关的集中配置位于 [usb2can_config.h](/home/hpx/HPXLoco_5/hpm_work/usb2can/.worktrees/usb2can-canfd-mode/inc/usb2can_config.h)。

当前默认值包括：

- 协议帧头：`0xA5`
- CAN 仲裁相位波特率：`1000000 bps`
- CAN FD 数据相位波特率：`2000000 bps`
- USB 总线编号：`0`
- CDC IN 端点：`0x81`
- CDC OUT 端点：`0x01`
- CDC INT 端点：`0x83`
- USB 产品字符串：`USB2CAN Bridge`

## 默认行为

- 设备上电默认进入 `CAN2_STD`
- 模式切换通过 CDC 私有协议中的控制命令完成
- 模式切换不持久化，设备重启后恢复 `CAN2_STD`
- 切换成功后，设备只接受当前活动模式对应的数据命令

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
  业务负载

## 字节序

协议中的 16-bit 字段按小端序编码：

- `len` 为小端
- `crc16` 为小端
- `data[]` 中的 `can_id` 为小端
- `GET_CAPABILITY_RSP` 中的 `mode_bitmap` 为小端

## 模式定义

模式定义位于 [usb2can_types.h](/home/hpx/HPXLoco_5/hpm_work/usb2can/.worktrees/usb2can-canfd-mode/inc/usb2can_types.h)。

| 模式值 | 名称 | 含义 |
|---|---|---|
| `0x00` | `CAN2_STD` | 经典 CAN2.0 标准帧 |
| `0x01` | `CANFD_STD` | CAN FD 标准帧，`BRS=0` |
| `0x02` | `CANFD_STD_BRS` | CAN FD 标准帧，`BRS=1` |

## 命令字定义

命令字定义位于 [usb2can_types.h](/home/hpx/HPXLoco_5/hpm_work/usb2can/.worktrees/usb2can-canfd-mode/inc/usb2can_types.h)。

### 控制平面命令

| cmd 值 | 名称 | 方向 | 含义 |
|---|---|---|---|
| `0x10` | `CMD_GET_MODE` | Host -> Device | 查询当前活动模式 |
| `0x11` | `CMD_GET_MODE_RSP` | Device -> Host | 返回当前活动模式 |
| `0x12` | `CMD_SET_MODE` | Host -> Device | 请求切换活动模式 |
| `0x13` | `CMD_SET_MODE_RSP` | Device -> Host | 返回模式切换结果 |
| `0x14` | `CMD_GET_CAPABILITY` | Host -> Device | 查询设备支持的模式能力 |
| `0x15` | `CMD_GET_CAPABILITY_RSP` | Device -> Host | 返回设备能力信息 |

### 数据平面命令

| cmd 值 | 名称 | 方向 | 含义 |
|---|---|---|---|
| `0x01` | `CMD_CAN_TX` | Host -> Device | 在 `CAN2_STD` 模式下发送 1 条 CAN2.0 标准帧 |
| `0x02` | `CMD_CAN_RX_REPORT` | Device -> Host | 在 `CAN2_STD` 模式下上报 1 条 CAN2.0 标准帧 |
| `0x03` | `CMD_CANFD_TX` | Host -> Device | 在 `CANFD_STD/CANFD_STD_BRS` 模式下发送 1 条 CAN FD 标准帧 |
| `0x04` | `CMD_CANFD_RX_REPORT` | Device -> Host | 在 `CANFD_STD/CANFD_STD_BRS` 模式下上报 1 条 CAN FD 标准帧 |
| `0x7F` | `CMD_ERROR_REPORT` | Device -> Host | 上报解析、校验或底层收发错误 |

## 控制平面负载格式

### `CMD_GET_MODE`

- `len = 0`
- `data[]` 为空

### `CMD_GET_MODE_RSP`

```text
+-----------+
| mode_id   |
| 1 byte    |
+-----------+
```

### `CMD_SET_MODE`

```text
+-----------+
| mode_id   |
| 1 byte    |
+-----------+
```

### `CMD_SET_MODE_RSP`

```text
+-----------+-----------+
| status    | mode_id   |
| 1 byte    | 1 byte    |
+-----------+-----------+
```

说明：

- `status = 0x00` 表示切换成功
- `mode_id` 表示切换后实际生效的模式

### `CMD_GET_CAPABILITY`

- `len = 0`
- `data[]` 为空

### `CMD_GET_CAPABILITY_RSP`

```text
+----------------+--------------------+
| mode_bitmap    | max_canfd_len      |
| 2 bytes LE     | 1 byte             |
+----------------+--------------------+
```

当前能力位定义：

- `bit0`：支持 `CAN2_STD`
- `bit1`：支持 `CANFD_STD`
- `bit2`：支持 `CANFD_STD_BRS`

当前默认响应值为：

- `mode_bitmap = 0x0007`
- `max_canfd_len = 64`

## 数据平面负载格式

### CAN2 负载

`CMD_CAN_TX` 与 `CMD_CAN_RX_REPORT` 使用相同的负载布局：

```text
+------------+------+----------------+
| can_id     | dlc  | payload[dlc]   |
| 2 bytes LE |1 byte| 0~8 bytes      |
+------------+------+----------------+
```

字段约束：

- `can_id` 仅低 11 位有效，范围 `0x000 ~ 0x7FF`
- `dlc` 范围 `0 ~ 8`
- `payload` 长度等于 `dlc`

因此：

- `len = 3 + dlc`

### CAN FD 负载

`CMD_CANFD_TX` 与 `CMD_CANFD_RX_REPORT` 使用相同的负载布局：

```text
+------------+----------+----------------------+
| can_id     | data_len | payload[data_len]    |
| 2 bytes LE | 1 byte   | 0~64 bytes           |
+------------+----------+----------------------+
```

字段约束：

- `can_id` 仅低 11 位有效，范围 `0x000 ~ 0x7FF`
- `data_len` 为实际数据长度，不是 DLC 原始编码
- 当前只接受 CAN FD canonical 长度：
  - `0..8`
  - `12`
  - `16`
  - `20`
  - `24`
  - `32`
  - `48`
  - `64`

因此：

- `len = 3 + data_len`

### BRS 说明

- `CANFD_STD` 模式下，设备发送 CAN FD 帧时使用 `BRS=0`
- `CANFD_STD_BRS` 模式下，设备发送 CAN FD 帧时使用 `BRS=1`
- USB 协议里的 CAN FD 负载结构在这两个模式下相同，区别只在当前活动模式和底层 MCAN 配置

## 错误码定义

错误码定义位于 [usb2can_types.h](/home/hpx/HPXLoco_5/hpm_work/usb2can/.worktrees/usb2can-canfd-mode/inc/usb2can_types.h) 中的 `Usb2CanErrorCode`。

当设备发送 `CMD_ERROR_REPORT` 时：

- `len = 1`
- `data[0]` 为单字节错误码

| 错误码 | 名称 | 含义 |
|---|---|---|
| `0x00` | `None` | 无错误 |
| `0x01` | `InvalidArgument` | 参数非法，例如 `can_id > 0x7FF` |
| `0x02` | `BufferTooSmall` | 缓冲区空间不足 |
| `0x03` | `ChecksumError` | CRC8 或 CRC16 校验失败 |
| `0x04` | `LengthError` | `len`、`dlc`、`data_len` 或真实数据长度不匹配 |
| `0x05` | `NeedMoreData` | 当前分片尚未拼出完整协议包 |
| `0x06` | `Unsupported` | 命令字、模式或当前活动模式不支持 |
| `0x07` | `IoError` | USB 或 CAN 底层收发失败 |

## 模式切换规则

- 上电默认模式为 `CAN2_STD`
- 控制命令在任何模式下都可以发送
- `SET_MODE` 成功后，设备立即切换到新模式
- 切换成功后，旧模式数据命令会被拒绝
- 模式切换失败时，设备保留旧模式不变

## 主机工具

当前主机工具：

- [send_can_test.py](/home/hpx/HPXLoco_5/hpm_work/usb2can/.worktrees/usb2can-canfd-mode/tools/send_can_test.py)
- [recv_can_test.py](/home/hpx/HPXLoco_5/hpm_work/usb2can/.worktrees/usb2can-canfd-mode/tools/recv_can_test.py)

### 常用命令

查询当前模式：

```bash
python tools/send_can_test.py --query get-mode --read-response
```

查询能力：

```bash
python tools/send_can_test.py --query get-capability --read-response
```

切换到 `CANFD_STD`：

```bash
python tools/send_can_test.py --mode canfd --set-mode-only --read-response
```

切换到 `CANFD_STD_BRS`：

```bash
python tools/send_can_test.py --mode canfd-brs --set-mode-only --read-response
```

发送 1 条 12 字节 CAN FD 帧：

```bash
python tools/send_can_test.py --mode canfd --can-id 0x123 --data "00 01 02 03 04 05 06 07 08 09 0A 0B" --count 1 --read-response
```

监听设备回传：

```bash
python tools/recv_can_test.py --port /dev/ttyACM0
```

## 日志设计

当前固件关键日志包括：

### 模式与 MCAN 配置日志

- `[usb2can][can] init requested mode=...`
- `[usb2can][can] active mode=... baud=... baud_fd=... canfd=...`
- `[usb2can][can] reconfigure begin old=... new=...`

### App 层模式切换日志

- `[usb2can][app] active mode switched to ...`
- `[usb2can][app] mode switch failed requested=... status=... active=...`

### 模式不匹配日志

- `[usb2can][app] reject can2 cmd in active mode=...`
- `[usb2can][app] reject canfd cmd in active mode=...`

### 回传过滤日志

- `[usb2can][usb-tx-task] drop rx frame mode=... active=...`

### 发送失败日志

- `[usb2can][can] mcan_transmit_blocking failed`
- `[usb2can][can] mcan_transmit_blocking fd failed ...`

## 当前实现文件

- 顶层编排：[usb2can_app.c](/home/hpx/HPXLoco_5/hpm_work/usb2can/.worktrees/usb2can-canfd-mode/src/usb2can_app.c)
- 协议层：[usb2can_protocol.c](/home/hpx/HPXLoco_5/hpm_work/usb2can/.worktrees/usb2can-canfd-mode/src/usb2can_protocol.c)
- 桥接层：[usb2can_bridge.c](/home/hpx/HPXLoco_5/hpm_work/usb2can/.worktrees/usb2can-canfd-mode/src/usb2can_bridge.c)
- CAN 适配层：[usb2can_can.c](/home/hpx/HPXLoco_5/hpm_work/usb2can/.worktrees/usb2can-canfd-mode/src/usb2can_can.c)
- USB CDC 适配层：[cdc_acm.c](/home/hpx/HPXLoco_5/hpm_work/usb2can/.worktrees/usb2can-canfd-mode/src/cdc_acm.c)
- 主机发送工具：[send_can_test.py](/home/hpx/HPXLoco_5/hpm_work/usb2can/.worktrees/usb2can-canfd-mode/tools/send_can_test.py)
- 主机接收工具：[recv_can_test.py](/home/hpx/HPXLoco_5/hpm_work/usb2can/.worktrees/usb2can-canfd-mode/tools/recv_can_test.py)
