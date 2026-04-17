# USB2CAN Stress Test Plan

Updated: 2026-04-17

## 1. Goal

This plan validates the stability of the current `usb2can` implementation in:

- `CAN2_STD`, `CANFD_STD`, and `CANFD_STD_BRS` mode switching
- sustained `USB -> CAN` transmission
- sustained `CAN -> USB` reporting
- `64-byte CAN FD` payload scenarios
- mixed mode switching and data traffic

## 2. Preconditions

- firmware has been flashed from the current worktree build
- host has activated the `usb2can` conda environment
- `/dev/ttyACM0` is accessible
- a CAN/CAN FD analyzer or peer device is available
- board-side debug logs are visible

Recommended setup:

```bash
cd /home/hpx/HPXLoco_5/hpm_work/usb2can/.worktrees/usb2can-canfd-mode
source "$HOME/miniconda3/etc/profile.d/conda.sh"
conda activate usb2can
```

## 3. Basic Control Plane Checks

### 3.1 Query current mode

```bash
python tools/send_can_test.py --query get-mode --read-response
```

Expected:

- `GET_MODE_RSP`
- default power-on mode `0x00`

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
python tools/send_can_test.py --mode can2 --can-id 0x123 --data "11 22 33 44" --count 100 --interval 0
```

Observe:

- analyzer receives 100 frames continuously
- no unexpected error reports
- no obvious frame loss

### 5.2 CAN FD 12-byte burst

```bash
python tools/send_can_test.py --mode canfd --can-id 0x123 --data "00 01 02 03 04 05 06 07 08 09 0A 0B" --count 100 --interval 0
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
python tools/send_can_test.py --mode canfd-brs --can-id 0x123 --data "$DATA64" --count 100 --interval 0
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

## 6. CAN -> USB Stress

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

## 8. Key Logs to Monitor

Record these logs during stress testing:

- `[usb2can][can] reconfigure begin old=... new=...`
- `[usb2can][can] active mode=...`
- `[usb2can][app] active mode switched to ...`
- `[usb2can][app] reject can2 cmd in active mode=...`
- `[usb2can][app] reject canfd cmd in active mode=...`
- `[usb2can][usb-rx-task] can tx ring overflow count=...`
- `[usb2can][usb-tx-task] can rx ring overflow count=...`
- `[usb2can][can] mcan_transmit_blocking failed`
- `[usb2can][can] mcan_transmit_blocking fd failed ...`

## 9. Pass Criteria

Stress testing should be considered passed only when all of the following hold:

- control-plane commands remain stable
- mode switching shows no mismatch or unintended rollback
- `CAN2`, `CANFD_STD`, and `CANFD_STD_BRS` all sustain data traffic
- `64-byte CAN FD` frames are not truncated
- no obvious frame loss during long or burst traffic
- board logs show no persistent errors or ring-buffer overflow
