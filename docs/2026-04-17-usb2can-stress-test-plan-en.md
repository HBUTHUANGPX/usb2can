# USB2CAN Stress Test Plan and Execution Record

Updated: 2026-04-21

## 1. Goal

This plan validates the stability of the current `usb2can` implementation in:

- `CAN2_STD`, `CANFD_STD`, and `CANFD_STD_BRS` mode switching
- sustained `USB -> CAN` transmission
- sustained `CAN -> USB` reporting
- `64-byte CAN FD` payload scenarios
- `CANFD_STD_BRS` extended-frame transmission/reporting
- mixed mode switching and data traffic

## 2. Preconditions

- firmware has been flashed from the current worktree build
- host has activated the `usb2can` conda environment
- `/dev/ttyACM0` is accessible
- a CAN/CAN FD analyzer or peer device is available
- board-side debug logs are visible

Recommended setup:

```bash
cd /home/hpx/HPXLoco_5/hpm_work/usb2can
source "$HOME/miniconda3/etc/profile.d/conda.sh"
conda activate usb2can
```

## 2.1 Automation Entry

The repository now includes a host-side stress runner:

```bash
python tools/run_stress_test.py --dry-run
```

By default it runs:

- `10` rounds of mode switching plus query verification
- `CAN2 4-byte` burst transmission
- `CANFD 12-byte` burst transmission
- `CANFD_BRS 64-byte` burst transmission
- `CANFD_BRS 12-byte` extended-frame burst transmission
- a confirmed `SET_MODE --read-response` before each burst
- `0.001s` default spacing between burst frames

You can also select only specific scenarios, for example:

```bash
python tools/run_stress_test.py --scenarios mode-switch canfd-brs-burst
```

Recommended full bench-side stress command:

```bash
conda run -n usb2can python tools/run_stress_test.py --switch-loops 10 --burst-count 200 --scenarios mode-switch can2-burst canfd-burst canfd-brs-burst canfd-brs-ext-burst
```

Notes:

- keep the default `--burst-interval 0.001` for normal stability testing
- use `--burst-interval 0` only when intentionally triggering CAN TX ring overflow
- burst phases use `--skip-mode-select` internally so mode-switch confirmation does not affect data-frame counts

## 3. Basic Control Plane Checks

### 3.1 Query current mode

```bash
python tools/send_can_test.py --query get-mode --read-response
```

Expected:

- `GET_MODE_RSP`
- default power-on mode `0x02`, meaning `CANFD_STD_BRS`

### 3.2 Query capabilities

```bash
python tools/send_can_test.py --query get-capability --read-response
```

Expected:

- `mode_bitmap=0x0007`
- `max_canfd_length=64`

## 4. Mode Switch Stability

### 4.1 Single-step switching

```bash
python tools/send_can_test.py --mode canfd --set-mode-only --read-response
python tools/send_can_test.py --query get-mode --read-response
python tools/send_can_test.py --mode canfd-brs --set-mode-only --read-response
python tools/send_can_test.py --query get-mode --read-response
python tools/send_can_test.py --mode can2 --set-mode-only --read-response
python tools/send_can_test.py --query get-mode --read-response
```

Expected:

- every `SET_MODE_RSP` returns `status=0x00`
- queried mode matches the requested effective mode

### 4.2 Repeated switching

Repeat the sequence below 20 times:

- `can2 -> canfd -> canfd-brs -> can2`

After each change, run:

```bash
python tools/send_can_test.py --query get-mode --read-response
```

Observe:

- no lock-up
- no unintended rollback
- board logs always show paired `reconfigure begin` and `active mode switched`

## 5. USB -> CAN Stress

### 5.1 Classic CAN burst

```bash
python tools/send_can_test.py --mode can2 --can-id 0x123 --data "11 22 33 44" --count 100 --interval 0.001
```

Observe:

- analyzer receives 100 frames continuously
- no unexpected error reports
- no obvious frame loss

Use `--interval 0` mainly for maximum host-write or ring-overflow tests.

### 5.2 CAN FD 12-byte burst

```bash
python tools/send_can_test.py --mode canfd --can-id 0x123 --data "00 01 02 03 04 05 06 07 08 09 0A 0B" --count 100 --interval 0.001
```

Observe:

- analyzer receives 100 frames
- each frame length is `12`
- `BRS=0`

### 5.3 CAN FD BRS 64-byte burst

```bash
DATA64="$(python3 - <<'PY'
print(' '.join(f'{i:02X}' for i in range(64)))
PY
)"
python tools/send_can_test.py --mode canfd-brs --can-id 0x123 --data "$DATA64" --count 100 --interval 0.001
```

Observe:

- analyzer receives 100 frames
- each frame length is `64`
- `BRS=1`
- no send failure logs

### 5.4 Batched CDC write test

```bash
python tools/send_can_test.py --mode canfd --can-id 0x123 --data "00 01 02 03 04 05 06 07 08 09 0A 0B" --count 100 --interval 0 --pack-count
```

Observe:

- firmware consumes the batched CDC write correctly
- no parser error
- no CAN TX ring overflow
- this is a maximum-write-pressure test, not the default normal stability pace

### 5.5 CAN FD BRS extended-frame burst

```bash
python tools/send_can_test.py --mode canfd-brs --frame-format ext --can-id 0x8001 --data "00 01 02 03 04 05 06 07 08 09 0A 0B" --count 100 --interval 0.001
```

Observe:

- analyzer receives 100 frames continuously
- frame format is CAN FD extended ID, ID `0x8001`
- each frame length is `12`
- `BRS=1`
- board logs show no `mcan_transmit_blocking fd ext failed`

## 6. CAN -> USB Stress

For receive-side tests, prefer an already activated conda environment and run
Python directly. This avoids `conda run` output capture/buffering hiding
real-time receive prints:

```bash
conda activate usb2can
python -u tools/recv_can_test.py --port /dev/ttyACM0
```

In CAN FD / CAN FD BRS mode, the board startup log should include
`rxfifo0=24 rxfifo1=0 rxbuf=0`. This confirms the receive-priority message RAM
layout is active. For zero-interval analyzer bursts, start with 100 frames, then
increase gradually after confirming there is no `rxfifo0 full` or `rxfifo0 lost`
log.

### 6.1 Classic CAN reporting

Switch mode first:

```bash
python tools/send_can_test.py --mode can2 --set-mode-only --read-response
python tools/recv_can_test.py --port /dev/ttyACM0
```

Then inject 100 classic CAN standard frames from the external CAN side.

Observe:

- host continuously prints `CAN_RX`
- ID, DLC, and payload stay correct

### 6.2 CAN FD reporting

Switch mode first:

```bash
python tools/send_can_test.py --mode canfd --set-mode-only --read-response
python tools/recv_can_test.py --port /dev/ttyACM0
```

Then inject 100 12-byte CAN FD standard frames externally.

Observe:

- host continuously prints `CANFD_RX`
- `len=12`
- no truncation

### 6.3 CAN FD BRS reporting

Switch mode first:

```bash
python tools/send_can_test.py --mode canfd-brs --set-mode-only --read-response
python tools/recv_can_test.py --port /dev/ttyACM0
```

Then inject 100 64-byte CAN FD BRS standard frames externally.

Observe:

- host continuously prints `CANFD_RX`
- `len=64`
- no truncation, no stalls, no error packets

### 6.4 CAN FD BRS extended-frame reporting

Switch mode first:

```bash
python tools/send_can_test.py --mode canfd-brs --set-mode-only --read-response
python tools/recv_can_test.py --port /dev/ttyACM0
```

Then inject 100 CAN FD BRS extended frames externally, for example ID `0x8001`
with a 12-byte payload.

Observe:

- host continuously prints `CANFD_EXT_RX`
- ID is displayed as `0x00008001`
- `len=12`
- no truncation or frame mixing

## 7. Mode Mismatch Tests

### 7.1 Send CAN2 data command while device is in CAN FD mode

```bash
python tools/send_can_test.py --mode canfd --set-mode-only --read-response
python tools/send_can_test.py --mode can2 --can-id 0x123 --data "11 22 33 44" --count 1 --read-response
```

Expected:

- device reports an error or unsupported-mode result
- classic CAN frame must not appear on the CAN bus
- board log should include `reject can2 cmd in active mode=...`

### 7.2 Send CAN FD data command while device is in CAN2 mode

```bash
python tools/send_can_test.py --mode can2 --set-mode-only --read-response
python tools/send_can_test.py --mode canfd --can-id 0x123 --data "00 01 02 03 04 05 06 07 08 09 0A 0B" --count 1 --read-response
```

Expected:

- device reports an error or unsupported-mode result
- CAN FD frame must not appear on the CAN bus

### 7.3 Send extended-frame data command outside `CANFD_STD_BRS`

```bash
python tools/send_can_test.py --mode canfd --set-mode-only --read-response
python tools/send_can_test.py --mode canfd-brs --skip-mode-select --frame-format ext --can-id 0x8001 --data "00 01 02 03 04 05 06 07 08 09 0A 0B" --count 1 --read-response
```

Expected:

- device reports an error or unsupported-mode result
- no extended frame appears on the CAN bus
- board log should include `reject canfd ext cmd in active mode=...`

## 8. Key Logs to Monitor

Record these logs during stress testing:

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

## 9. Pass Criteria

Stress testing should be considered passed only when all of the following hold:

- control-plane commands remain stable
- mode switching shows no mismatch or unintended rollback
- `CAN2`, `CANFD_STD`, and `CANFD_STD_BRS` all sustain data traffic
- `CANFD_STD_BRS` extended frames sustain transmission/reporting
- `64-byte CAN FD` frames are not truncated
- no obvious frame loss during long or burst traffic
- board logs show no persistent errors or ring-buffer overflow

## 10. Executed Results on 2026-04-17

The following checks were already executed against the current worktree firmware and `/dev/ttyACM0` environment:

### 10.1 Control plane and mode switching

- completed the basic `get-mode`, `get-capability`, and `set-mode(can2/canfd/canfd-brs)` checks
- completed `10` rounds of automated mode-switch regression
- each round used: `canfd -> query -> canfd-brs -> query -> can2 -> query`
- all rounds returned the expected `SET_MODE_RSP` and `GET_MODE_RSP`

### 10.2 USB -> CAN host-side burst transmission

- completed `CAN2 4-byte x 50`, host-side send succeeded
- completed `CANFD 12-byte x 50`, host-side send succeeded
- `CANFD_BRS 64-byte x 50` should be rechecked on your bench with a bus analyzer to confirm on-bus integrity

### 10.3 Board-log conclusion

The following log sequence was observed repeatedly and stably:

- `[usb2can][can] reconfigure begin old=... new=...`
- `[usb2can][can] active mode=...`
- `[usb2can][app] active mode switched to ...`

The following issues were not observed as persistent failures:

- `ring overflow`
- `mcan_transmit_blocking failed`
- mode/query mismatch after switching

### 10.4 Recommended bench-side follow-up

These items are still best validated on your flashed hardware with a real bus setup:

- confirm `BRS=1` during `CANFD_BRS 64-byte` capture
- run `100`-frame and `1000`-frame `CAN -> USB` reporting tests
- confirm that mode-mismatch cases truly produce no bus traffic

## 11. Additional Results on 2026-04-21

The following checks were completed on `main` firmware with default
`CANFD_STD_BRS`, `1 Mbps/80%` nominal timing, `5 Mbps/75%` data timing, and TDC
enabled:

- boot logs confirm the default active mode is `mode=2`
- the CAN analyzer can capture `CANFD_STD_BRS` data frames
- `--burst-count 500 --burst-interval 0` triggers `can tx ring overflow`, as expected for maximum host-write pressure
- the default `--burst-interval 0.001` avoids ring overflow on the current bench
- the full stress command was executed:

```bash
conda run -n usb2can python tools/run_stress_test.py --switch-loops 10 --burst-count 200 --scenarios mode-switch can2-burst canfd-burst canfd-brs-burst
```

Analyzer result:

- `0..199` are `CAN2.0` frames
- `200..399` are `CANFD` frames
- `400..599` are `CANFD BRS` frames

Conclusion:

- after separating mode confirmation from burst data transmission, all three
  data planes can be captured reliably at `200` frames each
- the default `0.001s` burst interval is the current bench stability baseline
- use explicit `--burst-interval 0` when testing firmware ring-overflow logging

## 12. Extended-Frame Follow-Up on 2026-04-21

After adding CAN FD BRS extended-frame support, run this additional stress item:

```bash
conda run -n usb2can python tools/run_stress_test.py --switch-loops 0 --burst-count 200 --scenarios canfd-brs-ext-burst
```

Single-frame smoke command:

```bash
python tools/send_can_test.py --mode canfd-brs --frame-format ext --can-id 0x8001 --data "00 01 02 03 04 05 06 07 08 09 0A 0B" --count 1 --read-response
```

Expected:

- CAN analyzer shows `CAN FD`, `Extended ID`, and `BRS=1`
- ID is `0x8001`
- DLC corresponds to a 12-byte payload
- firmware logs show no `reject canfd ext cmd` or `fd ext failed`
