# USB2CAN CANFD BRS Extended Frame Design

## Goal

Add transparent CAN FD BRS extended-frame support for motor integration without moving motor register parsing into MCU firmware.

## Scope

- Keep existing CAN2.0 standard-frame and CAN FD standard-frame commands compatible.
- Add CAN FD extended-frame transmit and receive report commands.
- Support 29-bit extended identifiers in CAN FD BRS mode.
- Keep BRS mode-driven: `CANFD_STD_BRS` sends BRS frames, not a per-frame flag.
- Do not parse or generate motor subframes in firmware.

## Protocol

Existing commands remain unchanged:

- `0x01 CMD_CAN_TX`: CAN2.0 standard frame.
- `0x02 CMD_CAN_RX_REPORT`: CAN2.0 standard-frame report.
- `0x03 CMD_CANFD_TX`: CAN FD standard frame.
- `0x04 CMD_CANFD_RX_REPORT`: CAN FD standard-frame report.

New commands:

- `0x05 CMD_CANFD_EXT_TX`: host requests one CAN FD extended frame.
- `0x06 CMD_CANFD_EXT_RX_REPORT`: device reports one CAN FD extended frame.

The new payload format is little-endian:

```text
| can_id u32 | data_len u8 | payload[data_len] |
```

Rules:

- `can_id <= 0x1FFFFFFF`.
- `data_len` must be one of `0..8, 12, 16, 20, 24, 32, 48, 64`.
- TX is accepted only while the active mode is `CANFD_STD_BRS`.

## Firmware Design

- Add a CAN FD extended-frame type with a `uint32_t can_id`.
- Add an `is_extended_id` flag and a 29-bit `can_id` to the internal bus-frame queue.
- Add bridge helpers to parse and serialize extended CAN FD payloads.
- Add `usb2can_can_send_fd_ext()` using `mcan_tx_frame_t.use_ext_id = 1` and `ext_id`.
- Update RX conversion to preserve `source->use_ext_id` and choose standard or extended report command.

## Host Tooling

- `tools/send_can_test.py` gains `--frame-format {std,ext}`, default `std`.
- Extended format is valid only with `--mode canfd-brs`.
- `tools/recv_can_test.py` decodes and prints `CMD_CANFD_EXT_RX_REPORT`.
- `tools/run_stress_test.py` gains a `canfd-brs-ext-burst` scenario.

## Tests

- C bridge tests cover extended payload round-trip, invalid 29-bit ID, invalid CAN FD length, and command constants.
- Python send tests cover extended frame encoding and argument validation.
- Python receive tests cover extended-frame decode and formatting.
- Stress runner tests cover the new extended burst command.

## Hardware Check

After flashing firmware:

```bash
conda run -n usb2can python tools/send_can_test.py \
  --mode canfd-brs --frame-format ext --can-id 0x8001 \
  --data "00 01 02 03 04 05 06 07 08 09 0A 0B" \
  --count 1 --read-response
```

The CAN analyzer should show one CAN FD BRS extended frame with ID `0x8001`.
