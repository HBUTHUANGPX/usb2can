# USB2CAN CAN FD Mode Design

## Background

The current `usb2can` project supports only `CAN2.0` standard data frames.
The next stage is to add `CAN FD` support while still limiting the scope to:

- standard identifier frames only
- optional `BRS` handling
- communication over the existing CDC private protocol

The user also wants the host and device to support switchable CAN communication
schemes. After switching, the host-side data structure may be completely
different from the previous scheme, so the protocol must treat each scheme as a
distinct active mode rather than trying to force all traffic into one shared
payload layout.

## Reference Implementations

The following local examples should be treated as implementation references
during development:

- `~/HPXLoco_5/hpm/hpm_sdk/samples/drivers/mcan/`
- `~/HPXLoco_5/hpm/hpm_sdk_extra/demos/cangaroo_hpmicro/`

The most relevant observed reference points are:

- `mcan_demo.c` demonstrates that CAN FD enablement is not just a frame flag;
  it also requires `can_config.enable_canfd = true` and
  `mcan_get_default_ram_config(..., true)` before `mcan_init()`
- `mcan_demo.c` uses `mcan_get_message_size_from_dlc()` when comparing and
  reading payload sizes, which confirms that DLC translation must not be treated
  as identical to actual payload length once CAN FD is introduced
- `cangaroo_hpmicro/protocol/slcan/src/slcan.c` contains
  `mcan_get_dlc_from_message_size()` and explicit `canfd_frame` /
  `bitrate_switch` handling, which is a useful reference for our own internal
  conversion helpers
- `cangaroo_hpmicro` also keeps receive ISR work small and leaves higher-level
  processing outside the IRQ path, which matches the current `usb2can`
  direction

## Confirmed Product Decisions

- Power-on default mode is `CAN2.0 standard frame`
- Mode switching happens at runtime over the CDC private protocol
- The selected mode is not persisted across power cycles
- After a mode switch succeeds, the device immediately enters the new mode
- Once switched, the device accepts only the active mode's data commands
- Frames for inactive modes are rejected with an explicit protocol error

## Goals

- Preserve today's `CAN2.0` behavior as the default startup experience
- Add `CAN FD standard frame` support
- Support an explicit `CAN FD + BRS` mode
- Make the private CDC protocol capable of querying and switching modes
- Keep protocol evolution extensible for future CAN transport schemes

## Non-Goals

- Extended CAN identifiers
- Remote frames
- CAN FD extended frames
- Persisting selected mode to non-volatile storage
- Simultaneously accepting multiple frame schemes at once
- Automatic mode negotiation on every CDC open

## Recommended Architecture

The protocol should be split into:

- a control plane for capability query and mode switching
- a data plane whose commands and payload layout are mode-specific

This avoids overloading the current `CAN2.0` payload format with new flags and
length semantics that would make parsing and debugging fragile.

### Why this approach

`CAN2.0` and `CAN FD` differ in several important ways:

- payload size range
- DLC meaning
- controller configuration requirements
- optional `BRS` behavior

Because the user explicitly wants data structures to be allowed to differ
between schemes, mode-specific commands are a better fit than one universal data
command with optional fields.

## Mode Model

Define a single runtime-active communication mode:

- `CAN2_STD`
- `CANFD_STD`
- `CANFD_STD_BRS`

Exactly one mode is active at a time.

### Runtime behavior

On boot:

- active mode = `CAN2_STD`

After `SET_MODE` success:

- active mode is updated immediately
- CAN controller is reconfigured for the new mode
- subsequent data packets are parsed only against the new mode's command set

If mode switching fails:

- the old mode remains active
- the host receives an explicit failure response

## Protocol Frame Shell

Keep the existing transport shell unchanged:

```text
+--------+-------+--------+------+--------+-----------+
| head   | cmd   | len    | crc8 | crc16  | data[len] |
| 1 byte | 1 byte| 2 bytes|1 byte|2 bytes | len bytes |
+--------+-------+--------+------+--------+-----------+
```

This preserves the current framing, buffering, stream parsing, and checksum
logic in both firmware and host tools.

## Command Set Design

### Control Commands

Add dedicated control commands independent of the active data mode.

Recommended commands:

- `CMD_GET_MODE`
- `CMD_GET_MODE_RSP`
- `CMD_SET_MODE`
- `CMD_SET_MODE_RSP`
- `CMD_GET_CAPABILITY`
- `CMD_GET_CAPABILITY_RSP`

These commands are always valid regardless of current active mode.

### Data Commands

Keep `CAN2.0` and `CAN FD` traffic separated by command.

Recommended commands:

- `CMD_CAN2_TX`
- `CMD_CAN2_RX_REPORT`
- `CMD_CANFD_TX`
- `CMD_CANFD_RX_REPORT`
- keep `CMD_ERROR_REPORT`

This means:

- in `CAN2_STD` mode, only `CMD_CAN2_TX` is accepted for host-to-device traffic
- in `CANFD_STD` mode, only `CMD_CANFD_TX` is accepted
- in `CANFD_STD_BRS` mode, only `CMD_CANFD_TX` is accepted, but the controller
  runs with the BRS-capable configuration for this mode

## Payload Design

### Control Payloads

#### `SET_MODE`

Recommended request payload:

```text
+-----------+
| mode_id   |
| 1 byte    |
+-----------+
```

Recommended response payload:

```text
+-----------+-----------+
| status    | mode_id   |
| 1 byte    | 1 byte    |
+-----------+-----------+
```

Where:

- `status = 0` means success
- non-zero status means switch rejected or reconfiguration failed
- `mode_id` returns the effective mode after processing

#### `GET_MODE_RSP`

Recommended payload:

```text
+-----------+
| mode_id   |
| 1 byte    |
+-----------+
```

#### `GET_CAPABILITY_RSP`

Recommended initial payload:

```text
+----------------+--------------------+
| mode_bitmap    | max_canfd_len      |
| 2 bytes LE     | 1 byte             |
+----------------+--------------------+
```

Meaning:

- `mode_bitmap` advertises supported runtime modes
- `max_canfd_len` advertises the largest supported CAN FD payload

This leaves room to evolve later without forcing the host to guess hardware
support.

### CAN2 Payload

Keep the existing `CAN2.0` payload unchanged:

```text
+------------+------+----------------+
| can_id     | dlc  | payload[dlc]   |
| 2 bytes LE |1 byte| 0~8 bytes      |
+------------+------+----------------+
```

This preserves backward compatibility for the default mode.

### CAN FD Payload

Define a dedicated CAN FD payload layout rather than extending CAN2.

Recommended payload:

```text
+------------+----------+--------------------+
| can_id     | data_len | payload[data_len]  |
| 2 bytes LE | 1 byte   | 0~64 bytes         |
+------------+----------+--------------------+
```

Notes:

- still standard identifier only, so `can_id <= 0x7FF`
- `data_len` is the actual byte count, not the raw CAN FD DLC encoding
- valid lengths should follow CAN FD data length rules supported by firmware
- the bridge layer should convert `data_len` to controller-specific DLC

This keeps the host protocol easy to use and isolates DLC encoding rules inside
firmware and host helpers.

## BRS Handling

Treat `BRS` as its own communication mode rather than a per-frame field in the
host payload.

### Reasoning

- The user asked for switchable communication schemes
- `CANFD_STD` and `CANFD_STD_BRS` can legitimately use different host behavior
- A mode-level distinction keeps the active runtime contract explicit
- Firmware-side controller configuration becomes simpler and easier to debug

### Consequence

`CMD_CANFD_TX` payload stays identical in:

- `CANFD_STD`
- `CANFD_STD_BRS`

The difference is in the active controller configuration and how outgoing frames
are emitted on the CAN bus.

## Firmware Design Changes

### `inc/usb2can_types.h`

Add:

- new command identifiers
- communication mode enum
- capability bitmap definitions
- CAN FD frame structure
- error code for mode mismatch if desired

Recommended new frame type:

```c
typedef struct Usb2CanFdStandardFrame {
  uint16_t can_id;
  uint8_t data_length;
  uint8_t payload[64];
} Usb2CanFdStandardFrame;
```

### `inc/usb2can_app.h` and `src/usb2can_app.c`

Add:

- current active mode state
- control-command dispatch path
- mode-specific data-command dispatch
- mode switch procedure with rollback on failure

Behavioral rules:

- control commands remain globally valid
- data commands are validated against current mode
- inactive-mode traffic results in protocol error reporting
- mode switch must not leave parsing state and CAN controller state out of sync

### `src/usb2can_bridge.c`

Split payload conversion helpers into:

- existing `CAN2` payload decode/encode
- new `CAN FD` payload decode/encode

Important detail:

- CAN FD helpers should validate allowed payload lengths
- DLC translation should be handled centrally and not duplicated across files

### `inc/usb2can_can.h` and `src/usb2can_can.c`

Extend the CAN adapter to support:

- `CAN2.0` controller configuration
- `CAN FD` controller configuration
- `CAN FD + BRS` controller configuration
- sending and receiving CAN FD standard frames

Needed behavior:

- expose a controller reconfigure API for mode switch
- leave active mode unchanged if low-level reconfiguration fails
- translate controller RX messages into either CAN2 or CAN FD internal frames
- follow the MCAN sample pattern for enabling FD RAM configuration before init

### `src/usb2can_protocol.c`

The outer transport shell remains unchanged, but implementation must continue to
preserve these invariants:

- parser remains stream-safe across chunk boundaries
- parser rejects oversized payloads before copying them into internal buffers
- control and data commands share the same framing and checksum rules
- introducing new commands must not change behavior for existing CAN2 frames

### `src/cdc_acm.c`

No structural protocol change is required here because the framing shell remains
unchanged, but buffer sizing must be reviewed.

Important capacity implication:

- current CDC TX/RX and protocol buffers are sized around small CAN2 packets
- CAN FD packets can carry up to 64 bytes payload
- all packet, payload, and queue-copy buffers must be re-validated

## Buffer and Capacity Impact

Current configuration values are too close to CAN2-sized traffic assumptions.

Files to review:

- `inc/usb2can_config.h`
- `src/usb2can_app.c`
- `src/cdc_acm.c`

Areas likely needing expansion:

- protocol frame buffer
- protocol TX payload buffer
- protocol TX frame buffer
- queue message copy sizes for USB RX chunks
- host tool read chunk assumptions

Even though a single CAN FD protocol frame still fits within current 512-byte CDC
buffers, the internal protocol buffers should be resized intentionally rather
than relying on incidental headroom.

## Implementation Discipline and Normative Rules

The following rules should be treated as implementation constraints, not just
style suggestions.

### 1. Keep control plane and data plane separate

- Mode query/switch commands must not share payload formats with CAN data
  commands
- Data commands must remain mode-specific after switching
- Avoid "one payload with optional fields for every mode"; this will make host
  parsing and firmware validation harder to reason about

### 2. Keep one source of truth for active mode

- Maintain a single runtime `active_mode`
- Parse and dispatch data commands strictly against that mode
- Do not infer mode from payload layout, payload length, or frame flags

### 3. Make mode switching atomic from protocol perspective

Recommended order:

1. validate request
2. compute target CAN configuration
3. quiesce or pause CAN traffic path
4. reconfigure MCAN
5. update `active_mode`
6. re-enable traffic path
7. reply success

Failure rule:

- if any low-level step fails, keep the previous mode active and reply failure

### 4. Centralize CAN FD length conversion

CAN FD adds two related but different concepts:

- application-visible payload byte length
- controller-visible DLC

Rules:

- host protocol should carry byte length, not raw DLC
- one helper should convert `data_length -> DLC`
- one helper should convert `DLC -> payload_length`
- do not duplicate this mapping in multiple modules

This is directly supported by the reference implementations, where controller
logic uses `mcan_get_message_size_from_dlc()` and cangaroo adds an explicit
`mcan_get_dlc_from_message_size()`.

### 5. Restrict accepted CAN FD payload sizes deliberately

Recommended accepted lengths:

- `0..8`
- `12`
- `16`
- `20`
- `24`
- `32`
- `48`
- `64`

Rules:

- reject non-canonical sizes like `9`, `10`, `11`, `13`, `14`, `15`, etc.
- return a stable protocol error instead of silently rounding

### 6. Do not put blocking or heavy encoding work into ISR

Maintain the current project rule:

- ISR only drains hardware FIFO / records frame / notifies task
- protocol encode/decode and USB send remain in task context

This is especially important for CAN FD because larger payloads increase the
cost of copying and encoding.

### 7. Recheck every buffer assumption when adding CAN FD

Any place that currently assumes "CAN payload max 8 bytes" must be audited.

Audit targets include:

- frame structs
- queue element sizes
- protocol frame buffers
- bridge temporary payload buffers
- CDC send buffers
- test fixtures and helper constants

### 8. Preserve CAN2 default compatibility

Required compatibility rule:

- with no mode switch command issued, existing CAN2 behavior must remain
  unchanged from the current firmware contract

### 9. Keep host and firmware enums aligned

Commands, mode ids, error codes, and capability bits must be defined in a way
that host tools can mirror exactly.

Recommended discipline:

- define mode ids and command ids once in firmware headers
- mirror them explicitly in host tools and tests
- document them in `README_zh.md` after implementation

### 10. Separate "mode unsupported" from generic transport failure

When possible, distinguish:

- invalid command
- inactive-mode packet
- unsupported mode
- low-level CAN reconfiguration failure
- generic I/O failure

This makes host-side debugging much easier during bring-up.

## Error Handling

Recommended error conditions:

- unsupported command
- invalid mode identifier
- mode unsupported by current firmware or hardware
- low-level CAN reconfiguration failure
- packet for inactive mode
- invalid CAN FD payload length
- invalid standard identifier

Recommended protocol-visible behavior:

- keep `CMD_ERROR_REPORT`
- optionally add a dedicated `ModeMismatch` error code

If the existing error enum is preferred, `Unsupported` can be used initially,
but a specific mismatch code would help host-side debugging.

## Host Tool Design

### `tools/send_can_test.py`

Evolve from a single-purpose CAN2 sender into a mode-aware test utility.

Recommended additions:

- send `GET_MODE`
- send `SET_MODE`
- build `CAN2` data packets
- build `CAN FD` data packets
- choose active scheme explicitly from CLI

Suggested CLI direction:

- `--mode can2`
- `--mode canfd`
- `--mode canfd-brs`

The tool should not assume that `CAN FD` payload shares the existing `CAN2`
packet layout.

### `tools/recv_can_test.py`

Add parsing for:

- mode responses
- capability responses
- CAN2 RX reports
- CAN FD RX reports
- error reports

The stream parser can stay unchanged because outer framing remains unchanged.

## Testing Strategy

Testing must cover not only happy-path data transmission but also the mode
state machine, reconfiguration failure handling, and CAN FD length rules.

### Firmware Unit Tests

Add tests for:

- mode command encode/decode
- CAN FD payload encode/decode
- mode mismatch rejection
- invalid CAN FD data length rejection
- capability response generation
- CAN FD canonical length acceptance table
- `data_length -> DLC` conversion
- `DLC -> data_length` conversion
- response behavior when mode switch reconfiguration fails

Recommended concrete unit cases:

- `CANFD data_length=0` encodes and decodes successfully
- `CANFD data_length=8` encodes and decodes successfully
- `CANFD data_length=12` encodes and decodes successfully
- `CANFD data_length=64` encodes and decodes successfully
- `CANFD data_length=9` is rejected
- `CANFD data_length=15` is rejected
- `CANFD data_length=63` is rejected
- `SET_MODE(CAN2_STD)` from `CAN2_STD` is idempotent and succeeds
- `SET_MODE(CANFD_STD)` updates effective mode only when low-level callback
  reports success
- `SET_MODE(unknown_mode)` is rejected without changing current mode

### Host Tool Tests

Add tests for:

- `SET_MODE` packet construction
- `GET_MODE` packet parsing
- `CAN FD` frame packet construction
- mode-aware RX report decoding
- capability response parsing
- rejection or reporting of inactive-mode responses
- CAN FD payload length validation before packet emission

Recommended concrete host cases:

- CLI `--mode can2` builds `SET_MODE(CAN2_STD)` or skips switch when requested
- CLI `--mode canfd` builds `SET_MODE(CANFD_STD)`
- CLI `--mode canfd-brs` builds `SET_MODE(CANFD_STD_BRS)`
- sending `64` bytes in CAN FD mode succeeds
- sending `15` bytes in CAN FD mode fails fast on host side
- RX parser correctly decodes CAN FD payload lengths from `0`, `12`, and `64`

### Integration Tests

Verify:

- boot defaults to `CAN2_STD`
- existing CAN2 host flow still works unchanged after boot
- successful switch to `CANFD_STD`
- successful switch to `CANFD_STD_BRS`
- inactive-mode data command is rejected after switching
- mode switch failure preserves previous mode

Recommended integration test content:

- `CAN2 default path`
  - no mode switch
  - host sends existing CAN2 packet
  - bus observes standard CAN2 frame
  - RX report remains in CAN2 format
- `CANFD_STD path`
  - host sends `SET_MODE(CANFD_STD)`
  - device acknowledges effective mode
  - host sends standard-id CAN FD frame with `12` bytes payload
  - bus observes CAN FD frame with `BRS=0`
  - device RX report uses CAN FD report command and CAN FD payload layout
- `CANFD_STD_BRS path`
  - host sends `SET_MODE(CANFD_STD_BRS)`
  - host sends standard-id CAN FD frame with `64` bytes payload
  - bus observes CAN FD frame with `BRS=1`
- `mode mismatch rejection`
  - switch to `CANFD_STD`
  - send old `CAN2` data command
  - device emits error report and does not transmit onto CAN bus
- `switch rollback`
  - inject or simulate CAN reconfiguration failure
  - send `SET_MODE(CANFD_STD)`
  - device replies failure
  - follow-up `GET_MODE` still returns previous mode

### Performance and Stress Tests

Because the project already cares about burst behavior, CAN FD support must be
verified under load instead of only with single-frame manual checks.

Required stress coverage:

- burst CAN FD RX to USB reporting
- burst USB to CAN FD sending
- mixed control + data traffic around mode switch boundaries
- repeated mode switching without reboot

Recommended stress cases:

- send `100` CAN FD frames with `64`-byte payloads from host in `CANFD_STD`
- send `100` CAN FD BRS frames with `64`-byte payloads in `CANFD_STD_BRS`
- inject repeated `GET_MODE` while streaming data and ensure no parser drift
- switch `CAN2 -> CANFD -> CAN2 -> CANFD_BRS` in a loop and verify no stuck
  state, no silent downgrade, and no queue corruption

### Hardware Bring-Up Checklist

During first board-level bring-up, explicitly confirm:

- MCAN init succeeds in CAN2 mode
- MCAN re-init succeeds in CAN FD mode
- MCAN re-init succeeds in CAN FD BRS mode
- correct nominal bitrate is applied
- correct data bitrate is applied for FD/BRS modes
- standard ID filter path still works
- RX FIFO drain logic still empties FIFO under CAN FD burst traffic
- USB report path does not truncate 64-byte payloads

### Regression Guardrails

The following existing behaviors must be re-run before claiming completion:

- current Python unit tests for `send_can_test.py`
- current Python unit tests for `recv_can_test.py`
- existing CAN2 functional path from host to device
- existing CAN2 functional path from device to host

No CAN FD work should be considered complete if CAN2 default behavior regresses.

## Migration Strategy

Recommended implementation order:

1. Add protocol enums, mode model, and control commands
2. Add mode query and mode switch handling without CAN FD traffic enabled yet
3. Add CAN FD internal frame types and bridge helpers
4. Add CAN adapter support for FD and BRS reconfiguration
5. Add host tool support for control commands and CAN FD data commands
6. Expand tests and update documentation

This keeps the work incremental and preserves a working default path throughout
the transition.

## Open Implementation Choices

These should be resolved during implementation planning:

- whether to introduce a dedicated `ModeMismatch` error code now or later
- exact capability bitmap bit assignments
- whether CAN FD valid lengths are restricted to canonical FD sizes only
- exact MCAN reinitialization API boundary in `usb2can_can.*`

## Final Recommendation

Implement CAN FD by introducing a mode-based protocol framework on top of the
existing CDC private framing shell.

The key design rules are:

- default to `CAN2_STD`
- add control commands for mode query and switching
- separate `CAN2` and `CAN FD` data commands
- treat `CANFD_STD_BRS` as a distinct active mode
- reject inactive-mode packets immediately

This approach preserves today's default behavior, keeps protocol parsing
unambiguous, and gives the project a clean path for future transport-scheme
expansion.
