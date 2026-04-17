# USB2CAN 压测方案

更新日期：2026-04-17

## 1. 目标

本方案用于验证当前 `usb2can` 在以下场景下的稳定性：

- `CAN2_STD`、`CANFD_STD`、`CANFD_STD_BRS` 模式切换稳定性
- `USB -> CAN` 连续下发稳定性
- `CAN -> USB` 连续回传稳定性
- `CAN FD 64-byte` 大负载场景
- 模式切换与数据流混合场景

## 2. 测试前提

- 固件已烧录为当前 worktree 构建产物
- 主机进入 `usb2can` conda 环境
- 可访问 `/dev/ttyACM0`
- 具备 CAN/CAN FD 抓包器或对端设备
- 可查看板级调试日志

建议先进入目录：

```bash
cd /home/hpx/HPXLoco_5/hpm_work/usb2can/.worktrees/usb2can-canfd-mode
source "$HOME/miniconda3/etc/profile.d/conda.sh"
conda activate usb2can
```

## 3. 基础控制面检查

### 3.1 查询当前模式

```bash
python tools/send_can_test.py --query get-mode --read-response
```

预期：

- 返回 `GET_MODE_RSP`
- 上电默认 `mode=0x00`

### 3.2 查询能力

```bash
python tools/send_can_test.py --query get-capability --read-response
```

预期：

- `mode_bitmap=0x0007`
- `max_canfd_length=64`

## 4. 模式切换稳定性测试

### 4.1 单次切换

```bash
python tools/send_can_test.py --mode canfd --set-mode-only --read-response
python tools/send_can_test.py --query get-mode --read-response
python tools/send_can_test.py --mode canfd-brs --set-mode-only --read-response
python tools/send_can_test.py --query get-mode --read-response
python tools/send_can_test.py --mode can2 --set-mode-only --read-response
python tools/send_can_test.py --query get-mode --read-response
```

预期：

- 每次 `SET_MODE_RSP status=0x00`
- 查询模式和切换后的模式一致

### 4.2 循环切换

手工循环执行 20 次以下顺序：

- `can2 -> canfd -> canfd-brs -> can2`

每次循环后插入：

```bash
python tools/send_can_test.py --query get-mode --read-response
```

观察点：

- 是否有卡死
- 是否出现模式回滚错误
- 板级日志是否始终成对出现 `reconfigure begin` 与 `active mode switched`

## 5. USB -> CAN 压测

### 5.1 CAN2 连发

```bash
python tools/send_can_test.py --mode can2 --can-id 0x123 --data "11 22 33 44" --count 100 --interval 0
```

观察点：

- 抓包器是否连续收到 100 帧
- 是否有错误上报
- 是否有明显丢帧

### 5.2 CAN FD 12-byte 连发

```bash
python tools/send_can_test.py --mode canfd --can-id 0x123 --data "00 01 02 03 04 05 06 07 08 09 0A 0B" --count 100 --interval 0
```

观察点：

- 抓包器连续收到 100 帧
- 每帧长度 `12`
- `BRS=0`

### 5.3 CAN FD BRS 64-byte 连发

```bash
DATA64="$(python3 - <<'PY'
print(' '.join(f'{i:02X}' for i in range(64)))
PY
)"
python tools/send_can_test.py --mode canfd-brs --can-id 0x123 --data "$DATA64" --count 100 --interval 0
```

观察点：

- 抓包器连续收到 100 帧
- 每帧长度 `64`
- `BRS=1`
- 板级日志中无发送失败

### 5.4 批量下发测试

```bash
python tools/send_can_test.py --mode canfd --can-id 0x123 --data "00 01 02 03 04 05 06 07 08 09 0A 0B" --count 100 --interval 0 --pack-count
```

观察点：

- 批量 CDC 写入后，固件能否完整消费
- 是否出现协议解析错误
- 是否出现 CAN TX ring overflow

## 6. CAN -> USB 压测

### 6.1 CAN2 回传

先切模式：

```bash
python tools/send_can_test.py --mode can2 --set-mode-only --read-response
python tools/recv_can_test.py --port /dev/ttyACM0
```

然后从外部总线侧连续发送 100 帧 CAN2 标准帧。

观察点：

- 主机终端连续打印 `CAN_RX`
- ID、DLC、payload 不错乱

### 6.2 CAN FD 回传

先切模式：

```bash
python tools/send_can_test.py --mode canfd --set-mode-only --read-response
python tools/recv_can_test.py --port /dev/ttyACM0
```

然后从外部总线侧连续发送 100 帧 12-byte CAN FD 标准帧。

观察点：

- 主机终端连续打印 `CANFD_RX`
- `len=12`
- 数据不截断

### 6.3 CAN FD BRS 回传

先切模式：

```bash
python tools/send_can_test.py --mode canfd-brs --set-mode-only --read-response
python tools/recv_can_test.py --port /dev/ttyACM0
```

然后从外部总线侧连续发送 100 帧 64-byte CAN FD BRS 标准帧。

观察点：

- 主机终端连续打印 `CANFD_RX`
- `len=64`
- 无截断、无卡顿、无错误报文

## 7. 模式错配测试

### 7.1 在 `CANFD` 模式下发送 CAN2 数据命令

```bash
python tools/send_can_test.py --mode canfd --set-mode-only --read-response
python tools/send_can_test.py --mode can2 --can-id 0x123 --data "11 22 33 44" --count 1 --read-response
```

预期：

- 设备返回错误上报或模式不支持
- CAN 总线上不应发出该经典 CAN 数据帧
- 板级日志应出现 `reject can2 cmd in active mode=...`

### 7.2 在 `CAN2` 模式下发送 CAN FD 数据命令

```bash
python tools/send_can_test.py --mode can2 --set-mode-only --read-response
python tools/send_can_test.py --mode canfd --can-id 0x123 --data "00 01 02 03 04 05 06 07 08 09 0A 0B" --count 1 --read-response
```

预期：

- 设备返回错误上报或模式不支持
- CAN 总线上不应发出 CAN FD 数据帧

## 8. 重点日志检查项

压测时建议同时记录以下日志：

- `[usb2can][can] reconfigure begin old=... new=...`
- `[usb2can][can] active mode=...`
- `[usb2can][app] active mode switched to ...`
- `[usb2can][app] reject can2 cmd in active mode=...`
- `[usb2can][app] reject canfd cmd in active mode=...`
- `[usb2can][usb-rx-task] can tx ring overflow count=...`
- `[usb2can][usb-tx-task] can rx ring overflow count=...`
- `[usb2can][can] mcan_transmit_blocking failed`
- `[usb2can][can] mcan_transmit_blocking fd failed ...`

## 9. 通过标准

建议以下条件同时满足才视为压测通过：

- 控制面命令持续稳定
- 模式切换无错位、无错误回滚
- `CAN2`、`CANFD_STD`、`CANFD_STD_BRS` 数据面都可持续工作
- `CAN FD 64-byte` 无截断
- 长时间或高频测试中无明显丢帧
- 板级日志中无持续性错误与 ring overflow
