# USB2CAN 压测方案与执行记录

更新日期：2026-04-21

## 1. 目标

本方案用于验证当前 `usb2can` 在以下场景下的稳定性：

- `CAN2_STD`、`CANFD_STD`、`CANFD_STD_BRS` 模式切换稳定性
- `USB -> CAN` 连续下发稳定性
- `CAN -> USB` 连续回传稳定性
- `CAN FD 64-byte` 大负载场景
- `CANFD_STD_BRS` 扩展帧收发场景
- 模式切换与数据流混合场景

## 2. 测试前提

- 固件已烧录为当前 worktree 构建产物
- 主机进入 `usb2can` conda 环境
- 可访问 `/dev/ttyACM0`
- 具备 CAN/CAN FD 抓包器或对端设备
- 可查看板级调试日志

建议先进入目录：

```bash
cd /home/hpx/HPXLoco_5/hpm_work/usb2can
source "$HOME/miniconda3/etc/profile.d/conda.sh"
conda activate usb2can
```

## 2.1 自动化脚本入口

当前仓库已提供主机侧自动压测脚本：

```bash
python tools/run_stress_test.py --dry-run
```

默认会执行：

- `10` 轮模式切换与查询校验
- `CAN2 4-byte` 突发发送
- `CANFD 12-byte` 突发发送
- `CANFD_BRS 64-byte` 突发发送
- `CANFD_BRS 12-byte` 扩展帧突发发送
- 每个 burst 前先执行 `SET_MODE --read-response`，确认目标模式后再发数据
- burst 默认帧间隔为 `0.001s`

也可以只跑指定场景，例如：

```bash
python tools/run_stress_test.py --scenarios mode-switch canfd-brs-burst
```

完整台架压测推荐命令：

```bash
conda run -n usb2can python tools/run_stress_test.py --switch-loops 10 --burst-count 200 --scenarios mode-switch can2-burst canfd-burst canfd-brs-burst canfd-brs-ext-burst
```

说明：

- 普通稳定性压测建议保留默认 `--burst-interval 0.001`
- `--burst-interval 0` 只用于专门触发和观察 CAN TX ring overflow
- burst 阶段内部使用 `--skip-mode-select`，避免模式切换确认阶段影响数据帧计数

## 3. 基础控制面检查

### 3.1 查询当前模式

```bash
python tools/send_can_test.py --query get-mode --read-response
```

预期：

- 返回 `GET_MODE_RSP`
- 上电默认 `mode=0x02`，即 `CANFD_STD_BRS`

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
python tools/send_can_test.py --mode can2 --can-id 0x123 --data "11 22 33 44" --count 100 --interval 0.001
```

观察点：

- 抓包器是否连续收到 100 帧
- 是否有错误上报
- 是否有明显丢帧

`--interval 0` 更适合作为极限写入或 ring overflow 触发测试。

### 5.2 CAN FD 12-byte 连发

```bash
python tools/send_can_test.py --mode canfd --can-id 0x123 --data "00 01 02 03 04 05 06 07 08 09 0A 0B" --count 100 --interval 0.001
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
python tools/send_can_test.py --mode canfd-brs --can-id 0x123 --data "$DATA64" --count 100 --interval 0.001
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
- 该项是极限写入测试，不作为普通稳定性压测的默认节奏

### 5.5 CAN FD BRS 扩展帧连发

```bash
python tools/send_can_test.py --mode canfd-brs --frame-format ext --can-id 0x8001 --data "00 01 02 03 04 05 06 07 08 09 0A 0B" --count 100 --interval 0.001
```

观察点：

- 抓包器连续收到 100 帧
- 帧格式为 CAN FD 扩展帧，ID 为 `0x8001`
- 每帧长度 `12`
- `BRS=1`
- 板级日志中无 `mcan_transmit_blocking fd ext failed`

## 6. CAN -> USB 压测

建议使用交互式 conda 环境直接执行 Python，避免 `conda run` 对长时间监听程序
做输出捕获：

```bash
conda activate usb2can
python -u tools/recv_can_test.py --port /dev/ttyACM0
```

CAN FD 模式启动日志中应出现 `rxfifo0=28 rxfifo1=0 rxbuf=0 txfifo=4`，
表示固件已使用接收优先的 MCAN message RAM 布局。若使用无间隔 burst 测试，
仍建议从 100 帧开始观察，再逐步提高帧数。

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

### 6.4 CAN FD BRS 扩展帧回传

先切模式：

```bash
python tools/send_can_test.py --mode canfd-brs --set-mode-only --read-response
python tools/recv_can_test.py --port /dev/ttyACM0
```

然后从外部总线侧连续发送 100 帧 CAN FD BRS 扩展帧，例如 ID `0x8001`、
12-byte payload。

观察点：

- 主机终端连续打印 `CANFD_EXT_RX`
- ID 显示为 `0x00008001`
- `len=12`
- 数据不截断、不串帧

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

### 7.3 在非 `CANFD_STD_BRS` 模式下发送扩展帧数据命令

```bash
python tools/send_can_test.py --mode canfd --set-mode-only --read-response
python tools/send_can_test.py --mode canfd-brs --skip-mode-select --frame-format ext --can-id 0x8001 --data "00 01 02 03 04 05 06 07 08 09 0A 0B" --count 1 --read-response
```

预期：

- 设备返回错误上报或模式不支持
- CAN 总线上不应发出扩展帧
- 板级日志应出现 `reject canfd ext cmd in active mode=...`

## 8. 重点日志检查项

压测时建议同时记录以下日志：

- `[usb2can][can] reconfigure begin old=... new=...`
- `[usb2can][can] active mode=... clock=... baud=... sp=... baud_fd=... sp_fd=... canfd=... tdc=... dbtp=... tdcr=...`
- `[usb2can][app] active mode switched to ...`
- `[usb2can][app] reject can2 cmd in active mode=...`
- `[usb2can][app] reject canfd cmd in active mode=...`
- `[usb2can][app] reject canfd ext cmd in active mode=...`
- `[usb2can][usb-rx-task] can tx ring overflow count=...`
- `[usb2can][usb-tx-task] can rx ring overflow count=...`
- `[usb2can][can] mcan_transmit_blocking failed`
- `[usb2can][can] mcan_transmit_blocking fd failed ...`
- `[usb2can][can] mcan_transmit_blocking fd ext failed ...`
- `[usb2can][can] tx fail status=... ir=... lec=... dlec=... tec=... rec=... busoff=... tdcv=...`

## 9. 通过标准

建议以下条件同时满足才视为压测通过：

- 控制面命令持续稳定
- 模式切换无错位、无错误回滚
- `CAN2`、`CANFD_STD`、`CANFD_STD_BRS` 数据面都可持续工作
- `CANFD_STD_BRS` 扩展帧可持续收发
- `CAN FD 64-byte` 无截断
- 长时间或高频测试中无明显丢帧
- 板级日志中无持续性错误与 ring overflow

## 10. 2026-04-17 已执行结果

以下内容已在当前 worktree 固件与 `/dev/ttyACM0` 环境下完成：

### 10.1 控制面与模式切换

- 已完成 `get-mode`、`get-capability`、`set-mode(can2/canfd/canfd-brs)` 基础检查
- 已完成 `10` 轮自动模式切换回归
- 每轮顺序为：`canfd -> query -> canfd-brs -> query -> can2 -> query`
- 实测所有轮次都返回正确 `SET_MODE_RSP` 与 `GET_MODE_RSP`

### 10.2 USB -> CAN 主机侧突发发送

- 已执行 `CAN2 4-byte x 50`，主机侧发送成功
- 已执行 `CANFD 12-byte x 50`，主机侧发送成功
- 已执行 `CANFD_BRS 64-byte x 50`，建议在你现场配合抓包器继续确认总线侧完整性

### 10.3 板级日志结论

已观察到以下日志序列稳定出现：

- `[usb2can][can] reconfigure begin old=... new=...`
- `[usb2can][can] active mode=...`
- `[usb2can][app] active mode switched to ...`

当前未观察到持续复现的：

- `ring overflow`
- `mcan_transmit_blocking failed`
- 模式切换后查询结果错位

### 10.4 建议你现场继续执行的项目

下面三项最适合在你烧录后的真实总线环境里继续确认：

- `CANFD_BRS 64-byte` 抓包确认 `BRS=1`
- `CAN -> USB` 方向的 `100` 帧与 `1000` 帧连续回传
- 模式错配测试下总线侧“确实不发帧”

## 11. 2026-04-21 补充验证结果

以下内容已在主分支、默认 `CANFD_STD_BRS`、仲裁相位 `1 Mbps/80%`、数据相位
`5 Mbps/75%`、TDC 开启的固件上完成：

- 上电日志确认默认模式为 `mode=2`
- CAN 分析仪已能看到 `CANFD_STD_BRS` 数据帧
- `--burst-count 500 --burst-interval 0` 会触发 `can tx ring overflow`，符合极限写入预期
- 默认 `--burst-interval 0.001` 下不再出现 ring overflow
- 已执行完整压测：

```bash
conda run -n usb2can python tools/run_stress_test.py --switch-loops 10 --burst-count 200 --scenarios mode-switch can2-burst canfd-burst canfd-brs-burst
```

抓包结果：

- `0..199` 为 `CAN2.0` 帧
- `200..399` 为 `CANFD` 帧
- `400..599` 为 `CANFD BRS` 帧

结论：

- 模式切换与 burst 数据发送解耦后，三种数据面各 `200` 帧均可稳定抓取
- 压测脚本的默认 `0.001s` 节流适合作为当前台架的稳定性基准
- 如需验证固件 ring overflow 日志，显式使用 `--burst-interval 0`

## 12. 2026-04-21 扩展帧补充测试项

新增 CAN FD BRS 扩展帧能力后，建议额外执行：

```bash
conda run -n usb2can python tools/run_stress_test.py --switch-loops 0 --burst-count 200 --scenarios canfd-brs-ext-burst
```

单帧冒烟命令：

```bash
python tools/send_can_test.py --mode canfd-brs --frame-format ext --can-id 0x8001 --data "00 01 02 03 04 05 06 07 08 09 0A 0B" --count 1 --read-response
```

预期：

- CAN 分析仪看到 `CAN FD`、`Extended ID`、`BRS=1`
- ID 为 `0x8001`
- DLC 对应 12-byte payload
- 固件无 `reject canfd ext cmd` 与 `fd ext failed` 日志
