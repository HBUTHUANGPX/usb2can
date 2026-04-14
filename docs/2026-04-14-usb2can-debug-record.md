# USB2CAN 调试与优化记录

更新日期：2026-04-14

## 1. 目标概述

本轮工作的核心目标是：

- 建立主机侧 Python 测试工具，覆盖 `USB -> CAN` 发送测试与 `CAN -> USB` 接收测试。
- 排查并修复 `recv_can_test.py` 初期只能打印第一帧的问题。
- 对照参考工程重新校核当前 `usb2can` 实现，确认真实瓶颈。
- 将 `CAN接收 -> USB发送` 路径优化为适合突发流量的结构。
- 重新审视 `USB接收 -> CAN发送` 路径，分析现有结构在低延迟与批量场景下的取舍。

本轮重点参考了以下代码：

- `/home/hpx/HPXLoco_5/hpm/hpm_sdk/samples/cherryusb/device/cdc_acm`
- `/home/hpx/HPXLoco_5/hpm/hpm_sdk/samples/drivers/mcan`
- `/home/hpx/HPXLoco_5/hpm/hpm_sdk_extra/demos/cangaroo_hpmicro`

其中后续优化主要参考了 `cangaroo_hpmicro` 的实现思路。

## 2. 主机侧工具建设

### 2.1 `tools/send_can_test.py`

已完成的能力：

- 构造 USB2CAN 协议帧并通过 CDC ACM 下发。
- 支持标准 CAN ID 与最多 8 字节 payload。
- 默认支持持续连发模式：
  - `--count 0` 表示持续发送，按 `Ctrl+C` 停止。
- 保留有限帧发送模式：
  - `--count N`
- 新增批量发送模式：
  - `--count N --pack-count`
  - 将 `N` 个完整协议帧打包成一个大 buffer，一次执行 `ser.write()`。

测试结论：

- 逐帧写入时，可用于观察固件 `USB接收 -> CAN发送` 链路的单帧固定开销。
- `--pack-count` 更接近后续业务的批量下发场景，可明显降低主机侧逐帧 CDC 写入引入的额外开销。

### 2.2 `tools/recv_can_test.py`

已完成的能力：

- 从 `/dev/ttyACM0` 持续读取设备回传的 USB2CAN 协议帧。
- 解码并在终端打印：
  - 时间戳
  - CAN ID
  - DLC
  - payload
  - raw 原始协议帧
- 时间戳已提升到微秒级。

为后续定位问题，接收工具还做过以下调试增强：

- 将 `timeout` 改为 `0`
- 将 `chunk size` 改为 `1024`

这些调整帮助确认了主机侧读取不是主要瓶颈。

### 2.3 Python 测试

已补充并通过：

- [test_send_can_test.py](/home/hpx/HPXLoco_5/hpm_work/usb2can/tests/test_send_can_test.py)
- [test_recv_can_test.py](/home/hpx/HPXLoco_5/hpm_work/usb2can/tests/test_recv_can_test.py)

其中 `send_can_test.py` 的测试覆盖了：

- 协议帧构造正确性
- 默认参数行为
- 有限发送模式参数
- `--pack-count` 参数行为

## 3. `CAN接收 -> USB发送` 路径排查与修复

### 3.1 初始问题

现象：

- 使用 CAN 抓包器发送多帧时，`recv_can_test.py` 初期只有第一次有终端打印。

后续通过单片机日志发现：

- ISR 中重复处理同一帧
- 单次发送一帧时，固件日志中 `rx_count` 持续增长
- 中断标志中反复出现 `0x00000002`

### 3.2 原因分析

结合参考工程与日志，确认问题主要出在 `usb2can_can.c` 的 MCAN 接收处理中：

- 早期实现没有正确按 FIFO 实际填充状态去持续搬空 FIFO。
- 对中断标志的理解不完整，尤其是 `RXFIFO0 watermark` 相关事件。
- `CAN接收 -> USB发送` 路径耦合过重，容易在中断与 USB 发送之间形成阻塞。

### 3.3 固件侧关键修改

#### `src/usb2can_can.c`

做过的关键调整：

- 重新梳理 MCAN 中断处理逻辑。
- 在 ISR 中根据 `RXFIFO0/RXFIFO1` 当前填充状态持续读空 FIFO。
- 增加并识别以下中断：
  - `MCAN_INT_RXFIFO0_FULL`
  - `MCAN_INT_RXFIFO0_MSG_LOST`
  - `MCAN_INT_RXFIFO0_WMK_REACHED`
  - `MCAN_INT_RXFIFO1_FULL`
  - `MCAN_INT_RXFIFO1_MSG_LOST`
  - `MCAN_INT_RXFIFO1_WMK_REACHED`
- 增加必要的调试打印，用于确认：
  - FIFO 是否被读空
  - 是否发生满/丢帧
  - 是否存在其他未预期中断标志

#### `src/usb2can_app.c`

参考 `cangaroo_hpmicro` 思路，将 `CAN接收 -> USB发送` 重构为：

- `CAN ISR + software ring buffer + FreeRTOS usb_tx_task`

具体实现：

- ISR 中只做最小动作：
  - 从 MCAN FIFO 读出标准帧
  - 写入 `CAN RX ring buffer`
  - 通知 `usb_tx_task`
- 在普通任务 `usb_tx_task` 中：
  - 从 ring buffer 取出 CAN 帧
  - 进行协议编码
  - 通过 USB CDC 发给主机

这条链路的目标是：

- 尽快清空 CAN 硬件 FIFO
- 避免在 ISR 中执行 USB 发送
- 提升 burst 场景下的稳态能力

#### `src/cdc_acm.c`

做过的关键调整：

- 使用发送忙标志和同步手段串行化 CDC IN 发送。
- 修复了 USB reset/disconnect 相关处理中的不安全信号释放问题。

早期曾出现异常：

```text
Exception occur!
mcause: 0x7
mepc: 0x14
mtval: 0xfffffff9
```

后续确认与不安全的同步处理有关，修复后问题消失。

### 3.4 验证结果

经过多轮测试，`CAN接收 -> USB发送` 方向已实现稳定接收：

- 定时每 5 秒发送 10 帧、ID 递增、payload 递增的测试可以连续收到。
- 连续 120 帧发送测试中，主机侧可连续打印大量递增 ID 的帧。
- 当 `recv_can_test.py` 使用非阻塞读取并加大 chunk 后，微秒级时间戳表明主机侧收包间隔很紧凑。

当前结论：

- `CAN接收 -> USB发送` 的主问题已经解决。
- 当前结构适合处理 CAN 侧 burst，并能及时清空硬件 FIFO。

## 4. `USB接收 -> CAN发送` 路径优化与重新评估

### 4.1 初始判断

在继续审视代码后，发现 `USB接收 -> CAN发送` 方向早期结构为：

- `usb_rx_task` 中解析协议
- 解析出一帧后直接调用 `usb2can_can_send()`
- `usb2can_can_send()` 内部调用 `mcan_transmit_blocking()`

这条路径优点是简单、单帧延迟低；缺点是：

- USB 侧如果快速下发多帧，`usb_rx_task` 可能被阻塞式 CAN 发送拖住。
- `USB RX queue` 深度较小，突发场景下可能溢出。

### 4.2 做过的结构化改动

为了和 `CAN接收 -> USB发送` 一样实现解耦，曾在 [usb2can_app.c](/home/hpx/HPXLoco_5/hpm_work/usb2can/src/usb2can_app.c) 中加入：

- `CAN TX ring buffer`
- `can_tx_task`

改造后的路径为：

- `usb_rx_task` 解析后先将标准帧压入 `CAN TX ring`
- 通知 `can_tx_task`
- `can_tx_task` 再调用 `usb2can_can_send()`

### 4.3 实验现象

用户使用新的 `send_can_test.py` 配合未修改固件测试：

- `--interval 0`
- 连发 10 帧
- 接收帧间隔约 `130-150 us`

用户使用修改后固件配合同样脚本测试：

- 同样条件下
- 接收帧间隔约 `180-200 us`

进一步使用 `--pack-count` 模式测试：

- 新固件的接收帧间隔可以稳定在 `130+ us`

### 4.4 当前结论

这说明：

- 新固件在 `USB接收 -> CAN发送` 方向变慢，并不是 MCAN 发送本身变慢。
- 主要额外开销来自：
  - ring buffer 入队/出队
  - 任务通知
  - 任务切换
  - 临界区进出
- 当主机逐帧 CDC 写入时，这部分固定调度成本会直接反映到单帧间隔上。
- 当主机使用批量下发时，这部分固定成本被摊薄，因此整体效果回到约 `130+ us`。

也就是说：

- `USB接收 -> CAN发送` 方向当前结构更偏吞吐保护，而不是极致单帧低时延。
- 对真实业务来说，如果主机场景主要是批量帧下发，那么当前结构并不一定是错误方向。

## 5. 当前已确认的业务判断

用户已明确：

- 主机通过 USB 下发数据在真实业务中不会很散。
- 后续主要应用场景会以批量帧发送为主。

因此当前更合理的优化目标应从“单帧极限延迟”切换为：

- 批量下发场景下的总吞吐
- 批量内部帧间隔稳定性
- 突发场景下的无丢帧能力
- 固件缓存与背压行为的可控性

## 6. 当前代码状态总结

截至本记录，已完成的核心成果如下：

- 已建立主机侧发送与接收测试脚本。
- 已补充对应 Python 单元测试。
- 已修复 `recv_can_test.py` 只能打印第一帧的问题。
- 已完成 `CAN接收 -> USB发送` 路径的结构化优化：
  - `ISR + ring buffer + usb_tx_task`
- 已修复 CDC 发送侧同步问题和相关异常。
- 已在 MCAN 中断处理中补齐 FIFO、watermark、full、lost 等关键事件处理。
- 已完成 `send_can_test.py` 的批量下发能力：
  - `--pack-count`
- 已通过实验确认：
  - 新固件在逐帧 CDC 下发时单帧间隔变大
  - 但在批量下发场景中性能恢复良好

## 7. 建议的后续工作

### 7.1 工具侧

建议继续增强 `send_can_test.py`，使其更适合作为正式压测工具：

- 支持起始 CAN ID
- 支持 ID 递增步进
- 支持 payload 递增
- 支持批量大小与批次数
- 支持发送统计输出

### 7.2 固件侧

建议根据真实业务场景决定 `USB接收 -> CAN发送` 的最终策略：

- 如果真实业务以批量下发为主：
  - 当前解耦结构可以保留，重点继续做 burst 稳定性验证。
- 如果仍需兼顾散发单帧最低延迟：
  - 可考虑后续实现混合策略：
    - 低负载时直发
    - backlog 时入 `CAN TX ring`

### 7.3 验证侧

建议后续形成固定对比测试矩阵：

- 主机逐帧发送
- 主机批量发送
- 固定 payload
- 递增 ID / 递增 payload
- 小 burst
- 大 burst

这样可以更快定位每次固件调整带来的真实收益或回归。

## 8. 相关文件

本轮重点涉及的文件：

- [src/usb2can_app.c](/home/hpx/HPXLoco_5/hpm_work/usb2can/src/usb2can_app.c)
- [src/usb2can_can.c](/home/hpx/HPXLoco_5/hpm_work/usb2can/src/usb2can_can.c)
- [src/cdc_acm.c](/home/hpx/HPXLoco_5/hpm_work/usb2can/src/cdc_acm.c)
- [tools/send_can_test.py](/home/hpx/HPXLoco_5/hpm_work/usb2can/tools/send_can_test.py)
- [tools/recv_can_test.py](/home/hpx/HPXLoco_5/hpm_work/usb2can/tools/recv_can_test.py)
- [tests/test_send_can_test.py](/home/hpx/HPXLoco_5/hpm_work/usb2can/tests/test_send_can_test.py)
- [tests/test_recv_can_test.py](/home/hpx/HPXLoco_5/hpm_work/usb2can/tests/test_recv_can_test.py)
