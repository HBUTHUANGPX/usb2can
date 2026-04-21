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

- CAN2.0 与普通 CAN FD 数据命令保持 `11-bit CAN ID` 标准帧
- CAN FD BRS 额外支持 `29-bit CAN ID` 扩展帧
- 不承载电机业务语义，只做协议转换
- `1 个 USB 协议包 <-> 1 条 CAN/CAN FD 帧`

## 集中配置项

与协议和链路相关的集中配置位于 [usb2can_config.h](inc/usb2can_config.h)。

当前默认值包括：

- 协议帧头：`0xA5`
- 上电默认模式：`CANFD_STD_BRS`
- CAN 仲裁相位波特率：`1000000 bps`
- CAN 仲裁相位采样点：`80%`
- CAN FD 数据相位波特率：`5000000 bps`
- CAN FD 数据相位采样点：`75%`
- CAN FD 发送延迟补偿：默认开启
- CAN FD TDC SSP 偏移/滤波窗口：`0`，表示由 HPM SDK 自动计算
- USB 总线编号：`0`
- CDC IN 端点：`0x81`
- CDC OUT 端点：`0x01`
- CDC INT 端点：`0x83`
- USB 产品字符串：`USB2CAN Bridge`

## 默认行为

- 设备上电默认进入 `CANFD_STD_BRS`
- 模式切换通过 CDC 私有协议中的控制命令完成
- 模式切换不持久化，设备重启后恢复 `CANFD_STD_BRS`
- 切换成功后，设备只接受当前活动模式对应的数据命令

## CAN FD 时序说明

当前默认 CAN FD BRS 时序针对现有台架配置：

- 仲裁相位：`1 Mbps`，采样点 `80%`
- 数据相位：`5 Mbps`，采样点 `75%`
- 默认开启 TDC，保证 `5 Mbps` BRS 发送稳定

如果 CAN 分析仪在 BRS 测试时提示数据相位位错误，优先确认分析仪的仲裁/数据
波特率和采样点是否一致。板级日志中应能看到 `tdc=1`，并且 MCAN 初始化后
`TDCR` 有非零有效值。

## 压测入口

- 主机侧自动压测脚本：`python tools/run_stress_test.py --dry-run`
- 中文压测方案与执行记录：[docs/2026-04-17-usb2can-stress-test-plan.md](docs/2026-04-17-usb2can-stress-test-plan.md)
- English stress plan and execution record: [docs/2026-04-17-usb2can-stress-test-plan-en.md](docs/2026-04-17-usb2can-stress-test-plan-en.md)

推荐完整台架压测命令：

```bash
conda run -n usb2can python tools/run_stress_test.py --switch-loops 10 --burst-count 200 --scenarios mode-switch can2-burst canfd-burst canfd-brs-burst canfd-brs-ext-burst
```

压测脚本会在每个 burst 前先确认目标模式，再使用 `--skip-mode-select` 发送纯
数据 burst。默认 `--burst-interval` 为 `0.001` 秒，避免主机写入速度超过 CAN
总线消耗速度而打满固件 CAN TX ring。只有在专门验证 ring overflow 行为时，才
建议使用 `--burst-interval 0`。

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
- `data[]` 中的标准帧 `can_id` 为 16-bit 小端
- `data[]` 中的扩展帧 `can_id` 为 32-bit 小端，仅低 29 位有效
- `GET_CAPABILITY_RSP` 中的 `mode_bitmap` 为小端

## 模式定义

模式定义位于 [usb2can_types.h](inc/usb2can_types.h)。

| 模式值 | 名称 | 含义 |
|---|---|---|
| `0x00` | `CAN2_STD` | 经典 CAN2.0 标准帧 |
| `0x01` | `CANFD_STD` | CAN FD 标准帧，`BRS=0` |
| `0x02` | `CANFD_STD_BRS` | CAN FD，标准帧或扩展帧，`BRS=1` |

## 命令字定义

命令字定义位于 [usb2can_types.h](inc/usb2can_types.h)。

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
| `0x05` | `CMD_CANFD_EXT_TX` | Host -> Device | 在 `CANFD_STD_BRS` 模式下发送 1 条 CAN FD 扩展帧 |
| `0x06` | `CMD_CANFD_EXT_RX_REPORT` | Device -> Host | 在 `CANFD_STD_BRS` 模式下上报 1 条 CAN FD 扩展帧 |
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

### CAN FD 扩展帧负载

`CMD_CANFD_EXT_TX` 与 `CMD_CANFD_EXT_RX_REPORT` 使用相同的负载布局：

```text
+------------+----------+----------------------+
| can_id     | data_len | payload[data_len]    |
| 4 bytes LE | 1 byte   | 0~64 bytes           |
+------------+----------+----------------------+
```

字段约束：

- `can_id` 仅低 29 位有效，范围 `0x00000000 ~ 0x1FFFFFFF`
- `data_len` 为实际数据长度，不是 DLC 原始编码
- 只接受 CAN FD canonical 长度：
  - `0..8`
  - `12`
  - `16`
  - `20`
  - `24`
  - `32`
  - `48`
  - `64`
- 当前扩展帧命令只在 `CANFD_STD_BRS` 模式下接受

因此：

- `len = 5 + data_len`

### BRS 说明

- `CANFD_STD` 模式下，设备发送 CAN FD 帧时使用 `BRS=0`
- `CANFD_STD_BRS` 模式下，设备发送 CAN FD 标准帧或扩展帧时使用 `BRS=1`
- BRS 由当前活动模式决定，USB 数据平面负载中不单独携带 per-frame BRS 标志

## 错误码定义

错误码定义位于 [usb2can_types.h](inc/usb2can_types.h) 中的 `Usb2CanErrorCode`。

当设备发送 `CMD_ERROR_REPORT` 时：

- `len = 1`
- `data[0]` 为单字节错误码

| 错误码 | 名称 | 含义 |
|---|---|---|
| `0x00` | `None` | 无错误 |
| `0x01` | `InvalidArgument` | 参数非法，例如标准帧 `can_id > 0x7FF` 或扩展帧 `can_id > 0x1FFFFFFF` |
| `0x02` | `BufferTooSmall` | 缓冲区空间不足 |
| `0x03` | `ChecksumError` | CRC8 或 CRC16 校验失败 |
| `0x04` | `LengthError` | `len`、`dlc`、`data_len` 或真实数据长度不匹配 |
| `0x05` | `NeedMoreData` | 当前分片尚未拼出完整协议包 |
| `0x06` | `Unsupported` | 命令字、模式或当前活动模式不支持 |
| `0x07` | `IoError` | USB 或 CAN 底层收发失败 |

## 模式切换规则

- 上电默认模式为 `CANFD_STD_BRS`
- 控制命令在任何模式下都可以发送
- `SET_MODE` 成功后，设备立即切换到新模式
- 切换成功后，旧模式数据命令会被拒绝
- 扩展帧数据命令只在 `CANFD_STD_BRS` 下接受；在 `CAN2_STD/CANFD_STD` 下会被拒绝
- 模式切换失败时，设备保留旧模式不变

## 主机工具

当前主机工具：

- [send_can_test.py](tools/send_can_test.py)
- [recv_can_test.py](tools/recv_can_test.py)

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

发送 1 条 12 字节 CAN FD BRS 扩展帧：

```bash
python tools/send_can_test.py --mode canfd-brs --frame-format ext --can-id 0x8001 --data "00 01 02 03 04 05 06 07 08 09 0A 0B" --count 1 --read-response
```

在不自动发送 `SET_MODE` 的情况下发送数据：

```bash
python tools/send_can_test.py --mode canfd-brs --skip-mode-select --can-id 0x123 --data "00 01 02 03 04 05 06 07" --count 10 --interval 0.001
```

`--skip-mode-select` 只应在已经确认设备处于目标模式后使用，例如先执行
`--set-mode-only --read-response`。压测脚本正是用这种方式把模式切换耗时从
burst 帧计数里剥离出来。

监听设备回传：

```bash
python tools/recv_can_test.py --port /dev/ttyACM0
```

如果通过 `conda run` 直接启动长时间监听脚本，建议加
`--no-capture-output`，否则 conda 可能缓存输出，导致终端看起来没有任何打印：

```bash
conda run --no-capture-output -n usb2can python -u tools/recv_can_test.py --port /dev/ttyACM0
```

## 日志设计

当前固件关键日志包括：

### 模式与 MCAN 配置日志

- `[usb2can][app] init protocol_head=... mode=... can_baudrate=... can_sp=... canfd_data_baudrate=... canfd_data_sp=... canfd_tdc=...`
- `[usb2can][can] init requested mode=... baud=... sp=... baud_fd=... sp_fd=... tdc=... tdco=... tdcf=...`
- `[usb2can][can] active mode=... clock=... baud=... sp=... baud_fd=... sp_fd=... canfd=... tdc=... tdco_cfg=... tdcf_cfg=... rxfifo0=... rxfifo1=... rxbuf=... txfifo=... dbtp=... tdcr=...`
- `[usb2can][can] reconfigure begin old=... new=...`
- `[usb2can][can] reconfigure recovering bus-off mode=...`
- `[usb2can][can] rx path rearmed reason=... flags_before=... flags_cleared=... fifo0_before=... fifo1_before=... drained0=... drained1=...`
- `[usb2can][can] reconfigure skipped mode=... unchanged rx_rearmed=1`

### App 层模式切换日志

- `[usb2can][app] active mode switched to ...`
- `[usb2can][app] mode switch failed requested=... status=... active=...`

### 模式不匹配日志

- `[usb2can][app] reject can2 cmd in active mode=...`
- `[usb2can][app] reject canfd cmd in active mode=...`
- `[usb2can][app] reject canfd ext cmd in active mode=...`

### 回传过滤日志

- `[usb2can][usb-tx-task] rx forward count=... mode=... ext=... id=... len=... cmd=...`
- `[usb2can][usb-tx-task] drop rx frame mode=... active=...`

`rx forward` 表示固件已经从 CAN 总线收到帧，并准备通过 USB CDC 回传给主机；
为避免压测刷屏，只打印前 8 帧以及之后每 1000 帧。

### 发送失败日志

- `[usb2can][can] mcan_transmit_blocking failed`
- `[usb2can][can] mcan_transmit_blocking fd failed ...`
- `[usb2can][can] mcan_transmit_blocking fd ext failed ...`
- `[usb2can][can] tx fail status=... ir=... lec=... dlec=... act=... tec=... rec=... busoff=... tdcv=...`

## 当前实现文件

- 顶层编排：[usb2can_app.c](src/usb2can_app.c)
- 协议层：[usb2can_protocol.c](src/usb2can_protocol.c)
- 桥接层：[usb2can_bridge.c](src/usb2can_bridge.c)
- CAN 适配层：[usb2can_can.c](src/usb2can_can.c)
- USB CDC 适配层：[cdc_acm.c](src/cdc_acm.c)
- 主机发送工具：[send_can_test.py](tools/send_can_test.py)
- 主机接收工具：[recv_can_test.py](tools/recv_can_test.py)
