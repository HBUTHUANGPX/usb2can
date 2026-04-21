# USB2CAN Host Protocol Guide

## Overview

`usb2can` is a minimal USB CDC ACM based bridge:

- The host sends private binary protocol packets to the MCU over USB CDC ACM.
- The MCU parses data commands according to the current active mode and forwards
  them to the CAN bus.
- The MCU receives CAN bus frames and reports them back to the host using the
  payload format of the current active mode.

The current firmware supports these runtime modes:

- `CAN2_STD`
- `CANFD_STD`
- `CANFD_STD_BRS`

Current scope:

- CAN2.0 and regular CAN FD data commands keep `11-bit CAN ID` standard frames
- CAN FD BRS additionally supports `29-bit CAN ID` extended frames
- no motor-specific business semantics
- `1 USB packet <-> 1 CAN/CAN FD frame`

## Central Configuration

Central protocol and link configuration lives in [usb2can_config.h](inc/usb2can_config.h).

Default values include:

- protocol head: `0xA5`
- power-on mode: `CANFD_STD_BRS`
- CAN nominal/arbitration bitrate: `1000000 bps`
- CAN nominal/arbitration sample point: `80%`
- CAN FD data bitrate: `5000000 bps`
- CAN FD data sample point: `75%`
- CAN FD transmitter delay compensation: enabled by default
- CAN FD TDC SSP offset/filter window: `0`, meaning HPM SDK auto calculation
- USB bus id: `0`
- CDC IN endpoint: `0x81`
- CDC OUT endpoint: `0x01`
- CDC INT endpoint: `0x83`
- USB product string: `USB2CAN Bridge`

## Default Behavior

- Power-on default mode is `CANFD_STD_BRS`
- Mode switching is done through CDC private protocol control commands
- Selected mode is not persisted
- After a successful switch, only the active mode's data commands are accepted

## CAN FD Timing Notes

The default CAN FD BRS timing is tuned for the current bench setup:

- nominal/arbitration phase: `1 Mbps`, `80%` sample point
- data phase: `5 Mbps`, `75%` sample point
- TDC is enabled to keep 5 Mbps BRS transmission stable

If a CAN analyzer reports data-phase bit errors during BRS testing, first verify
that the analyzer is configured with the same nominal/data bitrates and sample
points. Board logs should include `tdc=1` plus non-zero effective `TDCR` values
after MCAN initialization.

## Stress Entry Points

- Host-side stress runner: `python tools/run_stress_test.py --dry-run`
- Chinese stress plan and execution record: [docs/2026-04-17-usb2can-stress-test-plan.md](docs/2026-04-17-usb2can-stress-test-plan.md)
- English stress plan and execution record: [docs/2026-04-17-usb2can-stress-test-plan-en.md](docs/2026-04-17-usb2can-stress-test-plan-en.md)

Recommended full bench-side stress command:

```bash
conda run -n usb2can python tools/run_stress_test.py --switch-loops 10 --burst-count 200 --scenarios mode-switch can2-burst canfd-burst canfd-brs-burst canfd-brs-ext-burst
```

The stress runner confirms the target mode before each burst, then sends data
with `--skip-mode-select`. Its default `--burst-interval` is `0.001` seconds to
avoid filling the firmware CAN TX ring faster than the CAN bus can drain it.
Use `--burst-interval 0` only when intentionally testing ring overflow behavior.

## Wire Format

One complete USB protocol frame has this layout:

```text
+--------+-------+--------+------+--------+-----------+
| head   | cmd   | len    | crc8 | crc16  | data[len] |
| 1 byte | 1 byte| 2 bytes|1 byte|2 bytes | len bytes |
+--------+-------+--------+------+--------+-----------+
```

Field meanings:

- `head`: protocol head, default `0xA5`
- `cmd`: command id
- `len`: payload length, not including `crc16`
- `crc8`: CRC8 over `head + cmd + len`
- `crc16`: CRC16 over `data[len]`
- `data[len]`: payload

## Endianness

Multi-byte fields are little-endian:

- `len`
- `crc16`
- standard-frame `can_id` inside `data[]` is 16-bit little-endian
- extended-frame `can_id` inside `data[]` is 32-bit little-endian, with only
  the lower 29 bits valid
- `mode_bitmap` inside `GET_CAPABILITY_RSP`

## Mode Values

Mode definitions are in [usb2can_types.h](inc/usb2can_types.h).

| Value | Name | Meaning |
|---|---|---|
| `0x00` | `CAN2_STD` | Classic CAN2.0 standard frame |
| `0x01` | `CANFD_STD` | CAN FD standard frame with `BRS=0` |
| `0x02` | `CANFD_STD_BRS` | CAN FD standard or extended frame with `BRS=1` |

## Command Set

### Control Plane

| Value | Name | Direction | Meaning |
|---|---|---|---|
| `0x10` | `CMD_GET_MODE` | Host -> Device | Query current active mode |
| `0x11` | `CMD_GET_MODE_RSP` | Device -> Host | Return current active mode |
| `0x12` | `CMD_SET_MODE` | Host -> Device | Request mode switch |
| `0x13` | `CMD_SET_MODE_RSP` | Device -> Host | Return mode switch result |
| `0x14` | `CMD_GET_CAPABILITY` | Host -> Device | Query supported mode capability |
| `0x15` | `CMD_GET_CAPABILITY_RSP` | Device -> Host | Return capability information |

### Data Plane

| Value | Name | Direction | Meaning |
|---|---|---|---|
| `0x01` | `CMD_CAN_TX` | Host -> Device | Send one classic CAN standard frame in `CAN2_STD` mode |
| `0x02` | `CMD_CAN_RX_REPORT` | Device -> Host | Report one classic CAN standard frame in `CAN2_STD` mode |
| `0x03` | `CMD_CANFD_TX` | Host -> Device | Send one CAN FD standard frame in `CANFD_STD/CANFD_STD_BRS` mode |
| `0x04` | `CMD_CANFD_RX_REPORT` | Device -> Host | Report one CAN FD standard frame in `CANFD_STD/CANFD_STD_BRS` mode |
| `0x05` | `CMD_CANFD_EXT_TX` | Host -> Device | Send one CAN FD extended frame in `CANFD_STD_BRS` mode |
| `0x06` | `CMD_CANFD_EXT_RX_REPORT` | Device -> Host | Report one CAN FD extended frame in `CANFD_STD_BRS` mode |
| `0x7F` | `CMD_ERROR_REPORT` | Device -> Host | Report parser, validation, or low-level I/O errors |

## Control Plane Payloads

### `CMD_GET_MODE`

- `len = 0`
- empty payload

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

Notes:

- `status = 0x00` means success
- `mode_id` is the effective mode after handling the request

### `CMD_GET_CAPABILITY`

- `len = 0`
- empty payload

### `CMD_GET_CAPABILITY_RSP`

```text
+----------------+--------------------+
| mode_bitmap    | max_canfd_len      |
| 2 bytes LE     | 1 byte             |
+----------------+--------------------+
```

Current capability bits:

- `bit0`: `CAN2_STD`
- `bit1`: `CANFD_STD`
- `bit2`: `CANFD_STD_BRS`

Current default response:

- `mode_bitmap = 0x0007`
- `max_canfd_len = 64`

## Data Plane Payloads

### Classic CAN Payload

Used by `CMD_CAN_TX` and `CMD_CAN_RX_REPORT`:

```text
+------------+------+----------------+
| can_id     | dlc  | payload[dlc]   |
| 2 bytes LE |1 byte| 0~8 bytes      |
+------------+------+----------------+
```

Constraints:

- `can_id` range: `0x000 ~ 0x7FF`
- `dlc` range: `0 ~ 8`
- payload length must equal `dlc`

So:

- `len = 3 + dlc`

### CAN FD Payload

Used by `CMD_CANFD_TX` and `CMD_CANFD_RX_REPORT`:

```text
+------------+----------+----------------------+
| can_id     | data_len | payload[data_len]    |
| 2 bytes LE | 1 byte   | 0~64 bytes           |
+------------+----------+----------------------+
```

Constraints:

- `can_id` range: `0x000 ~ 0x7FF`
- `data_len` is the actual payload byte count, not raw DLC encoding
- accepted canonical CAN FD lengths:
  - `0..8`
  - `12`
  - `16`
  - `20`
  - `24`
  - `32`
  - `48`
  - `64`

So:

- `len = 3 + data_len`

### CAN FD Extended Payload

Used by `CMD_CANFD_EXT_TX` and `CMD_CANFD_EXT_RX_REPORT`:

```text
+------------+----------+----------------------+
| can_id     | data_len | payload[data_len]    |
| 4 bytes LE | 1 byte   | 0~64 bytes           |
+------------+----------+----------------------+
```

Constraints:

- `can_id` range: `0x00000000 ~ 0x1FFFFFFF`
- `data_len` is the actual payload byte count, not raw DLC encoding
- accepted canonical CAN FD lengths:
  - `0..8`
  - `12`
  - `16`
  - `20`
  - `24`
  - `32`
  - `48`
  - `64`
- the extended-frame command is currently accepted only in `CANFD_STD_BRS`

So:

- `len = 5 + data_len`

### BRS Behavior

- `CANFD_STD` sends CAN FD with `BRS=0`
- `CANFD_STD_BRS` sends CAN FD standard or extended frames with `BRS=1`
- BRS is mode-driven and is not carried as a per-frame flag in the USB payload

## Error Codes

Error codes are defined in [usb2can_types.h](inc/usb2can_types.h).

When the device sends `CMD_ERROR_REPORT`:

- `len = 1`
- `data[0]` is the error code

| Value | Name | Meaning |
|---|---|---|
| `0x00` | `None` | No error |
| `0x01` | `InvalidArgument` | Invalid parameter, such as standard `can_id > 0x7FF` or extended `can_id > 0x1FFFFFFF` |
| `0x02` | `BufferTooSmall` | Buffer is too small |
| `0x03` | `ChecksumError` | CRC8 or CRC16 failed |
| `0x04` | `LengthError` | `len`, `dlc`, `data_len`, or actual payload size mismatch |
| `0x05` | `NeedMoreData` | Current USB chunk does not yet form a full packet |
| `0x06` | `Unsupported` | Unsupported command, mode, or inactive-mode data command |
| `0x07` | `IoError` | USB or CAN low-level send/receive failure |

## Mode Switching Rules

- default power-on mode is `CANFD_STD_BRS`
- control-plane commands are valid in any mode
- after `SET_MODE` succeeds, the device immediately switches mode
- old-mode data commands are rejected after switching
- extended-frame data commands are accepted only in `CANFD_STD_BRS` and rejected
  in `CAN2_STD/CANFD_STD`
- if switching fails, the previous mode is kept

## Host Tools

Current host tools:

- [send_can_test.py](tools/send_can_test.py)
- [recv_can_test.py](tools/recv_can_test.py)

### Common Commands

Query current mode:

```bash
python tools/send_can_test.py --query get-mode --read-response
```

Query capabilities:

```bash
python tools/send_can_test.py --query get-capability --read-response
```

Switch to `CANFD_STD`:

```bash
python tools/send_can_test.py --mode canfd --set-mode-only --read-response
```

Switch to `CANFD_STD_BRS`:

```bash
python tools/send_can_test.py --mode canfd-brs --set-mode-only --read-response
```

Send one 12-byte CAN FD frame:

```bash
python tools/send_can_test.py --mode canfd --can-id 0x123 --data "00 01 02 03 04 05 06 07 08 09 0A 0B" --count 1 --read-response
```

Send one 12-byte CAN FD BRS extended frame:

```bash
python tools/send_can_test.py --mode canfd-brs --frame-format ext --can-id 0x8001 --data "00 01 02 03 04 05 06 07 08 09 0A 0B" --count 1 --read-response
```

Send data without an automatic `SET_MODE` first:

```bash
python tools/send_can_test.py --mode canfd-brs --skip-mode-select --can-id 0x123 --data "00 01 02 03 04 05 06 07" --count 10 --interval 0.001
```

Use `--skip-mode-select` only after the device mode has already been confirmed,
for example by `--set-mode-only --read-response`. This is how the stress runner
keeps mode-switch latency out of burst frame counts.

Listen for device reports:

```bash
python tools/recv_can_test.py --port /dev/ttyACM0
```

When starting the long-running receiver through `conda run`, prefer
`--no-capture-output`; otherwise conda may buffer stdout and the terminal can
look silent:

```bash
conda run --no-capture-output -n usb2can python -u tools/recv_can_test.py --port /dev/ttyACM0
```

## Logging Design

Current key firmware logs include:

### Mode and MCAN configuration logs

- `[usb2can][app] init protocol_head=... mode=... can_baudrate=... can_sp=... canfd_data_baudrate=... canfd_data_sp=... canfd_tdc=...`
- `[usb2can][can] init requested mode=... baud=... sp=... baud_fd=... sp_fd=... tdc=... tdco=... tdcf=...`
- `[usb2can][can] active mode=... clock=... baud=... sp=... baud_fd=... sp_fd=... canfd=... tdc=... tdco_cfg=... tdcf_cfg=... rxfifo0=... rxfifo1=... rxbuf=... dbtp=... tdcr=...`
- `[usb2can][can] reconfigure begin old=... new=...`
- `[usb2can][can] reconfigure recovering bus-off mode=...`
- `[usb2can][can] reconfigure skipped mode=... unchanged`

### App-level mode switch logs

- `[usb2can][app] active mode switched to ...`
- `[usb2can][app] mode switch failed requested=... status=... active=...`

### Mode mismatch logs

- `[usb2can][app] reject can2 cmd in active mode=...`
- `[usb2can][app] reject canfd cmd in active mode=...`
- `[usb2can][app] reject canfd ext cmd in active mode=...`

### RX forwarding filter logs

- `[usb2can][usb-tx-task] rx forward count=... mode=... ext=... id=... len=... cmd=...`
- `[usb2can][usb-tx-task] drop rx frame mode=... active=...`

`rx forward` means the firmware has received a CAN bus frame and is about to
report it to the host over USB CDC. To avoid excessive stress-test logging, it
prints only the first 8 frames and then every 1000th frame.

### Send failure logs

- `[usb2can][can] mcan_transmit_blocking failed`
- `[usb2can][can] mcan_transmit_blocking fd failed ...`
- `[usb2can][can] mcan_transmit_blocking fd ext failed ...`
- `[usb2can][can] tx fail status=... ir=... lec=... dlec=... act=... tec=... rec=... busoff=... tdcv=...`

## Current Implementation Files

- App orchestration: [usb2can_app.c](src/usb2can_app.c)
- Protocol layer: [usb2can_protocol.c](src/usb2can_protocol.c)
- Bridge layer: [usb2can_bridge.c](src/usb2can_bridge.c)
- CAN adapter: [usb2can_can.c](src/usb2can_can.c)
- USB CDC adapter: [cdc_acm.c](src/cdc_acm.c)
- Host sender: [send_can_test.py](tools/send_can_test.py)
- Host receiver: [recv_can_test.py](tools/recv_can_test.py)
