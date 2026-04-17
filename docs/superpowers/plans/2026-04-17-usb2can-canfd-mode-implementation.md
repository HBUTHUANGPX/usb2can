# USB2CAN CAN FD Mode Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add switchable `CAN2_STD`, `CANFD_STD`, and `CANFD_STD_BRS` communication modes over the existing CDC private protocol while preserving the current default `CAN2` behavior.

**Architecture:** Keep the existing USB transport shell unchanged, add a control-plane command set for querying and switching active mode, and split data-plane handling into mode-specific `CAN2` and `CAN FD` command/payload paths. Reconfigure MCAN when mode changes, keep ISR work minimal, and continue routing heavy protocol and USB work through FreeRTOS tasks.

**Tech Stack:** C, FreeRTOS, HPM SDK MCAN driver, CherryUSB CDC ACM, Python test utilities, `unittest`

---

## File Structure

### Files to modify

- `inc/usb2can_types.h`
  Responsibility: add mode enum, command ids, capability bits, optional new error codes, and CAN FD frame type.
- `inc/usb2can_config.h`
  Responsibility: enlarge protocol and payload buffer configuration for CAN FD traffic; optionally define CAN FD related defaults.
- `inc/usb2can_bridge.h`
  Responsibility: declare CAN FD payload encode/decode helpers and DLC/length mapping helpers.
- `inc/usb2can_can.h`
  Responsibility: declare mode-aware CAN init/reconfigure/send APIs and CAN FD RX callback types if needed.
- `inc/usb2can_app.h`
  Responsibility: expose any new app-level config or mode-related declarations if needed.
- `src/usb2can_bridge.c`
  Responsibility: implement CAN2/CAN FD payload translation plus centralized CAN FD length conversion.
- `src/usb2can_protocol.c`
  Responsibility: keep framing logic intact while ensuring new commands remain supported cleanly.
- `src/usb2can_can.c`
  Responsibility: add MCAN FD/BRS configuration, reconfigure support, CAN FD send path, and receive translation.
- `src/usb2can_app.c`
  Responsibility: add active mode state machine, control command dispatch, mode switching, and mode-aware TX/RX packet handling.
- `README_zh.md`
  Responsibility: document new mode commands, payloads, and host integration rules.
- `tools/send_can_test.py`
  Responsibility: add mode control commands and CAN FD send support.
- `tools/recv_can_test.py`
  Responsibility: add mode/capability response parsing and CAN FD RX report parsing.
- `tests/usb2can_protocol_test.c`
  Responsibility: extend C-side protocol and bridge tests for mode commands and CAN FD payload rules.
- `tests/test_send_can_test.py`
  Responsibility: extend host send tool coverage.
- `tests/test_recv_can_test.py`
  Responsibility: extend host receive tool coverage.

### Files to create

- `tests/usb2can_bridge_test.c`
  Responsibility: focused coverage for CAN FD payload encode/decode and length mapping if existing protocol tests become too crowded.
- `tests/usb2can_app_mode_test.c`
  Responsibility: focused mode-state-machine tests if the app logic is unit-testable in the current setup.

### Reference files to consult while implementing

- `~/HPXLoco_5/hpm/hpm_sdk/samples/drivers/mcan/src/mcan_demo.c`
- `~/HPXLoco_5/hpm/hpm_sdk_extra/demos/cangaroo_hpmicro/protocol/slcan/src/slcan.c`
- `docs/superpowers/specs/2026-04-17-usb2can-canfd-mode-design.md`

## Task 1: Lock protocol ids, mode ids, and frame types

**Files:**
- Modify: `inc/usb2can_types.h`
- Modify: `inc/usb2can_config.h`
- Test: `tests/usb2can_protocol_test.c`

- [ ] **Step 1: Add failing C tests for new enums and frame-level assumptions**

Add tests that assert:

- `CAN2_STD`, `CANFD_STD`, `CANFD_STD_BRS` mode ids are stable
- new command ids for `GET_MODE`, `SET_MODE`, `GET_CAPABILITY`, `CANFD_TX`, `CANFD_RX_REPORT` exist
- CAN FD max payload size is `64`
- current CAN2 max payload size remains `8`

- [ ] **Step 2: Run the protocol test target and capture the current failure**

Run the repo’s existing C test command for protocol tests. If no dedicated target exists yet, record the missing infrastructure and use the smallest available build/test command that exercises `tests/usb2can_protocol_test.c`.

Expected:

- compile failure or failing assertions because the new enums/types do not exist yet

- [ ] **Step 3: Add the new shared type definitions**

Update `inc/usb2can_types.h` to include:

- mode enum
- control-plane command ids
- CAN FD data-plane command ids
- capability bit definitions
- CAN FD frame struct with `payload[64]`
- optional `ModeMismatch` error code if you choose to add it now

Update `inc/usb2can_config.h` to add or clarify any constants needed by CAN FD support, especially buffer-related constants.

- [ ] **Step 4: Re-run the targeted C tests**

Expected:

- compile succeeds for the type additions
- new enum/type assertions pass

- [ ] **Step 5: Commit**

```bash
git add inc/usb2can_types.h inc/usb2can_config.h tests/usb2can_protocol_test.c
git commit -m "feat: add usb2can mode and canfd shared types"
```

## Task 2: Add centralized CAN FD length/DLC helpers and bridge coverage

**Files:**
- Modify: `inc/usb2can_bridge.h`
- Modify: `src/usb2can_bridge.c`
- Create: `tests/usb2can_bridge_test.c`

- [ ] **Step 1: Write failing bridge tests for CAN FD canonical lengths**

Add tests that verify:

- accepted lengths: `0, 1, ..., 8, 12, 16, 20, 24, 32, 48, 64`
- rejected lengths: `9, 10, 11, 13, 14, 15, 17, 63`
- `data_length -> DLC` mapping matches MCAN rules
- `DLC -> payload_length` mapping matches MCAN rules

- [ ] **Step 2: Run the bridge tests to verify failure**

Run:

```bash
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected:

- new bridge test fails or does not compile because helpers are missing

- [ ] **Step 3: Implement centralized helper functions**

In `src/usb2can_bridge.c`, add helpers equivalent in spirit to:

```c
bool usb2can_bridge_is_valid_canfd_length(uint8_t data_length);
Usb2CanStatus usb2can_bridge_canfd_length_to_dlc(uint8_t data_length, uint8_t *dlc);
Usb2CanStatus usb2can_bridge_canfd_dlc_to_length(uint8_t dlc, uint8_t *data_length);
```

Design rules:

- no duplicated lookup tables in other modules
- reject invalid lengths explicitly
- keep conversion logic independent of USB packet parsing

- [ ] **Step 4: Re-run tests and verify helper coverage passes**

Expected:

- all canonical mapping tests pass

- [ ] **Step 5: Commit**

```bash
git add inc/usb2can_bridge.h src/usb2can_bridge.c tests/usb2can_bridge_test.c
git commit -m "feat: add canfd length mapping helpers"
```

## Task 3: Add CAN FD payload encode/decode helpers

**Files:**
- Modify: `inc/usb2can_bridge.h`
- Modify: `src/usb2can_bridge.c`
- Modify: `tests/usb2can_bridge_test.c`

- [ ] **Step 1: Write failing tests for CAN FD payload serialization**

Add tests covering:

- encode `can_id=0x123`, `data_length=12`, payload `00..0B`
- encode `can_id=0x123`, `data_length=64`, payload `00..3F`
- decode matching payloads back into internal frame structs
- reject `can_id > 0x7FF`
- reject non-canonical `data_length`
- reject mismatched `length != 3 + data_length`

- [ ] **Step 2: Run tests to confirm failure**

Expected:

- missing symbols or failing assertions for CAN FD payload encode/decode

- [ ] **Step 3: Implement CAN FD bridge functions**

Add functions similar to:

```c
Usb2CanStatus usb2can_bridge_payload_to_canfd_frame(...);
Usb2CanStatus usb2can_bridge_canfd_frame_to_payload(...);
```

Rules:

- host protocol stores actual byte length, not raw DLC
- payload size validation uses the centralized helpers from Task 2
- keep CAN2 bridge code unchanged for default-mode compatibility

- [ ] **Step 4: Re-run the bridge tests**

Expected:

- CAN FD payload encode/decode tests pass
- CAN2 bridge behavior remains intact

- [ ] **Step 5: Commit**

```bash
git add inc/usb2can_bridge.h src/usb2can_bridge.c tests/usb2can_bridge_test.c
git commit -m "feat: add canfd payload bridge helpers"
```

## Task 4: Add protocol-level tests for control commands

**Files:**
- Modify: `tests/usb2can_protocol_test.c`
- Modify: `src/usb2can_protocol.c` (only if required)

- [ ] **Step 1: Write failing tests for control-plane packet encoding/decoding**

Add test cases for:

- `GET_MODE` request with zero-length payload
- `GET_MODE_RSP` with one-byte mode payload
- `SET_MODE` request with one-byte mode payload
- `SET_MODE_RSP` with `status + effective_mode`
- `GET_CAPABILITY_RSP` with `mode_bitmap + max_canfd_len`

- [ ] **Step 2: Run the tests to confirm any protocol assumptions that break**

Expected:

- existing protocol shell likely already works, but tests verify no hidden assumptions about old command ids or small payloads

- [ ] **Step 3: Adjust protocol tests or implementation only if the framing layer has hidden constraints**

Rules:

- do not change the outer frame shell
- do not special-case control commands in `usb2can_protocol.c` unless a real framing bug is found

- [ ] **Step 4: Re-run the protocol tests**

Expected:

- control-plane packets encode/decode successfully using the unchanged framing shell

- [ ] **Step 5: Commit**

```bash
git add tests/usb2can_protocol_test.c src/usb2can_protocol.c
git commit -m "test: add control command protocol coverage"
```

## Task 5: Extend the CAN adapter for CAN FD and BRS reconfiguration

**Files:**
- Modify: `inc/usb2can_can.h`
- Modify: `src/usb2can_can.c`

- [ ] **Step 1: Add failing low-level tests or compile-time checks for new adapter APIs**

If the repo supports C unit coverage here, add tests for:

- `usb2can_can_reconfigure(mode)`
- CAN FD send path API
- RX translation helpers for CAN FD

If unit-testing this layer directly is not practical, add a small compile-time test harness or note this layer will be verified primarily through integration tests.

- [ ] **Step 2: Implement a mode-aware MCAN configuration builder**

Add a helper that prepares `mcan_config_t` for:

- `CAN2_STD`
- `CANFD_STD`
- `CANFD_STD_BRS`

Rules drawn from the references:

- for CAN FD modes, set `enable_canfd = true`
- for CAN FD modes, call `mcan_get_default_ram_config(..., true)`
- nominal and data-phase bitrate values must be explicit and reviewable

- [ ] **Step 3: Implement CAN reconfigure API with rollback-friendly behavior**

Suggested structure:

- stop/deinit current controller
- build target config
- init target config
- re-enable interrupts
- report success/failure without updating app mode state here

Rules:

- this layer should not own `active_mode`
- leave ownership of logical mode state to `usb2can_app.c`

- [ ] **Step 4: Implement CAN FD send and RX translation**

Add:

- send path for internal CAN FD frame struct to `mcan_tx_frame_t`
- RX translation from `mcan_rx_message_t` to internal CAN FD frame

Rules:

- set `canfd_frame` and `bitrate_switch` based on the active hardware config or requested send path
- use centralized DLC helpers when converting payload lengths
- continue keeping ISR lightweight

- [ ] **Step 5: Run the build and any available low-level tests**

Run:

```bash
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected:

- build passes
- no CAN2 compile regressions

- [ ] **Step 6: Commit**

```bash
git add inc/usb2can_can.h src/usb2can_can.c
git commit -m "feat: add canfd and brs can adapter support"
```

## Task 6: Add app-level mode state machine and control command handling

**Files:**
- Modify: `inc/usb2can_app.h`
- Modify: `src/usb2can_app.c`
- Create: `tests/usb2can_app_mode_test.c`

- [ ] **Step 1: Write failing tests for mode-state behavior**

Add cases for:

- boot default mode is `CAN2_STD`
- `SET_MODE(CANFD_STD)` updates active mode only on low-level success
- `SET_MODE(CANFD_STD_BRS)` updates active mode only on low-level success
- `SET_MODE(invalid)` is rejected
- failed reconfigure preserves previous mode
- control commands are valid in any mode

- [ ] **Step 2: Run tests to confirm failure**

Expected:

- missing active-mode logic and missing control-plane dispatch

- [ ] **Step 3: Add active mode state and control-plane packet handling**

Implement in `src/usb2can_app.c`:

- active mode storage, defaulting to `CAN2_STD`
- `GET_MODE`, `SET_MODE`, and `GET_CAPABILITY` handlers
- control-plane response packet construction

Rules:

- control commands must be accepted regardless of active mode
- mode updates happen only after CAN reconfiguration succeeds
- response packets must reflect effective mode after processing

- [ ] **Step 4: Re-run tests**

Expected:

- mode-state tests pass

- [ ] **Step 5: Commit**

```bash
git add inc/usb2can_app.h src/usb2can_app.c tests/usb2can_app_mode_test.c
git commit -m "feat: add usb2can mode control state machine"
```

## Task 7: Make app-level data path mode-aware

**Files:**
- Modify: `src/usb2can_app.c`
- Modify: `src/usb2can_bridge.c` (only if small glue changes are needed)

- [ ] **Step 1: Write failing tests for mode-aware data command dispatch**

Cover:

- `CAN2_STD` accepts `CMD_CAN2_TX` and rejects `CMD_CANFD_TX`
- `CANFD_STD` accepts `CMD_CANFD_TX` and rejects `CMD_CAN2_TX`
- `CANFD_STD_BRS` accepts `CMD_CANFD_TX`
- CAN RX report command selection follows active receive mode

- [ ] **Step 2: Run tests to confirm failure**

Expected:

- old app logic still assumes one CAN2 command set

- [ ] **Step 3: Implement mode-aware data dispatch**

Rules:

- parser still consumes framing exactly as before
- inactive-mode packets return protocol error and must not reach CAN send path
- `CANFD_STD` and `CANFD_STD_BRS` share the same USB payload structure but differ in active controller behavior
- CAN RX reporting must choose `CMD_CAN2_RX_REPORT` vs `CMD_CANFD_RX_REPORT` based on the current active mode

- [ ] **Step 4: Verify task/ring-buffer behavior remains intact**

Run a build plus tests. Review that:

- ISR path still only buffers/notify
- CAN FD larger payloads do not introduce task-stack overflows or obvious buffer misuse

- [ ] **Step 5: Commit**

```bash
git add src/usb2can_app.c src/usb2can_bridge.c
git commit -m "feat: add mode-aware usb2can data dispatch"
```

## Task 8: Resize and audit buffers for CAN FD traffic

**Files:**
- Modify: `inc/usb2can_config.h`
- Modify: `src/usb2can_app.c`
- Modify: `src/cdc_acm.c`

- [ ] **Step 1: Add failing tests or assertions for max CAN FD protocol frame size**

Add coverage or static assertions for:

- max CAN FD payload = `64`
- full protocol frame size = `header + crc16 + 67`
- all internal packet buffers can hold that worst-case frame with margin

- [ ] **Step 2: Audit and update buffer sizes**

Review and update:

- protocol parse frame buffer
- protocol TX frame buffer
- protocol TX payload buffer
- any queue-copy struct members that assume tiny frames
- CDC send/receive scratch buffers if needed

- [ ] **Step 3: Re-run the build and tests**

Expected:

- no truncation-related failures
- no compiler warnings about size mismatches introduced by the changes

- [ ] **Step 4: Commit**

```bash
git add inc/usb2can_config.h src/usb2can_app.c src/cdc_acm.c
git commit -m "feat: enlarge usb2can buffers for canfd traffic"
```

## Task 9: Extend the host send utility for mode control and CAN FD TX

**Files:**
- Modify: `tools/send_can_test.py`
- Modify: `tests/test_send_can_test.py`

- [ ] **Step 1: Write failing Python tests for mode control and CAN FD packet building**

Add cases for:

- `build_set_mode_frame(CAN2_STD)`
- `build_set_mode_frame(CANFD_STD)`
- `build_set_mode_frame(CANFD_STD_BRS)`
- `build_canfd_protocol_frame(can_id, payload_12_bytes)`
- `build_canfd_protocol_frame(can_id, payload_64_bytes)`
- invalid `15`-byte CAN FD payload rejected
- CLI parsing for `--mode can2`, `--mode canfd`, `--mode canfd-brs`

- [ ] **Step 2: Run the Python send-tool tests and verify failure**

Run:

```bash
python -m unittest tests/test_send_can_test.py -v
```

Expected:

- failures because mode control and CAN FD packet support are not implemented yet

- [ ] **Step 3: Implement the send-tool changes**

Update `tools/send_can_test.py` to:

- define mirrored mode ids and command ids
- add helpers to build control-plane packets
- add CAN FD packet builder using actual byte length
- add CLI mode selection
- fail fast for invalid CAN FD lengths before serial write

- [ ] **Step 4: Re-run the Python tests**

Expected:

- all send-tool tests pass

- [ ] **Step 5: Commit**

```bash
git add tools/send_can_test.py tests/test_send_can_test.py
git commit -m "feat: add canfd mode control to send tool"
```

## Task 10: Extend the host receive utility for mode/capability and CAN FD RX parsing

**Files:**
- Modify: `tools/recv_can_test.py`
- Modify: `tests/test_recv_can_test.py`

- [ ] **Step 1: Write failing Python tests for new response and report parsing**

Add cases for:

- `GET_MODE_RSP`
- `SET_MODE_RSP`
- `GET_CAPABILITY_RSP`
- `CANFD_RX_REPORT` with `12` bytes
- `CANFD_RX_REPORT` with `64` bytes
- inactive-mode or unknown report handling remains stable

- [ ] **Step 2: Run the Python receive-tool tests and verify failure**

Run:

```bash
python -m unittest tests/test_recv_can_test.py -v
```

Expected:

- failures due to missing control-plane and CAN FD RX parsing

- [ ] **Step 3: Implement receive-tool parsing changes**

Update `tools/recv_can_test.py` to:

- add mirrored mode ids and command ids
- parse control-plane responses
- parse CAN FD RX reports
- format decoded CAN FD payloads cleanly
- keep the stream parser outer framing unchanged

- [ ] **Step 4: Re-run the Python tests**

Expected:

- all receive-tool tests pass

- [ ] **Step 5: Commit**

```bash
git add tools/recv_can_test.py tests/test_recv_can_test.py
git commit -m "feat: add canfd parsing to receive tool"
```

## Task 11: Update documentation and integration procedure

**Files:**
- Modify: `README_zh.md`
- Modify: `docs/2026-04-14-usb2can-debug-record.md` (only if a short addendum is helpful)

- [ ] **Step 1: Document the final protocol contract**

Update `README_zh.md` with:

- mode enum descriptions
- control-plane command list
- CAN2 payload format
- CAN FD payload format
- BRS mode behavior
- host-side mode switching expectations

- [ ] **Step 2: Add a short implementation note if real behavior diverged from the original debug record**

Only touch `docs/2026-04-14-usb2can-debug-record.md` if the final CAN FD implementation changes conclusions relevant to future debugging.

- [ ] **Step 3: Build/test one more time**

Run:

```bash
cmake --build build
ctest --test-dir build --output-on-failure
python -m unittest tests/test_send_can_test.py tests/test_recv_can_test.py -v
```

Expected:

- documentation changes do not break any tests

- [ ] **Step 4: Commit**

```bash
git add README_zh.md docs/2026-04-14-usb2can-debug-record.md
git commit -m "docs: describe usb2can canfd mode protocol"
```

## Task 12: Run board-level verification before claiming completion

**Files:**
- No code changes required unless failures are found

- [ ] **Step 1: Verify default CAN2 behavior on hardware**

Manual test:

- boot firmware
- do not send `SET_MODE`
- use existing host flow to send and receive CAN2 frames

Expected:

- behavior matches current baseline

- [ ] **Step 2: Verify `CANFD_STD` hardware path**

Manual test:

- send `SET_MODE(CANFD_STD)`
- confirm `GET_MODE` returns `CANFD_STD`
- send standard-id CAN FD frame with `12` bytes
- send standard-id CAN FD frame with `64` bytes

Expected:

- no truncation
- bus shows CAN FD with `BRS=0`

- [ ] **Step 3: Verify `CANFD_STD_BRS` hardware path**

Manual test:

- send `SET_MODE(CANFD_STD_BRS)`
- confirm `GET_MODE` returns `CANFD_STD_BRS`
- send standard-id CAN FD frame with `64` bytes

Expected:

- bus shows CAN FD with `BRS=1`

- [ ] **Step 4: Verify mode mismatch and rollback**

Manual test:

- in `CANFD_STD`, send old CAN2 data command
- confirm error report and no CAN transmit
- if you can simulate reconfigure failure, verify `SET_MODE` failure preserves previous mode

- [ ] **Step 5: Verify stress behavior**

Manual test:

- burst 100 CAN FD frames with `64` bytes payload in `CANFD_STD`
- burst 100 CAN FD frames with `64` bytes payload in `CANFD_STD_BRS`
- loop `CAN2 -> CANFD -> CAN2 -> CANFD_BRS`

Expected:

- no stuck mode
- no queue corruption
- no parser drift

- [ ] **Step 6: Final commit if fixes were required**

```bash
git add <changed-files>
git commit -m "fix: address canfd verification issues"
```

## Global Verification Checklist

Run before marking the full feature complete:

```bash
cmake --build build
ctest --test-dir build --output-on-failure
python -m unittest tests/test_send_can_test.py tests/test_recv_can_test.py -v
```

Manual verification:

- default CAN2 still works without switching mode
- `SET_MODE` success and failure behavior is correct
- CAN FD canonical lengths work
- invalid CAN FD lengths fail fast
- CAN FD/BRS hardware behavior matches the selected mode

## Notes for the implementing agent

- Preserve the current `CAN2` default path until the end; never break the boot-default behavior mid-series.
- Keep `data_length <-> DLC` conversion in one place only.
- Follow the reference samples for FD init: `enable_canfd = true` and
  `mcan_get_default_ram_config(..., true)` before `mcan_init()`.
- Keep logical mode state in the app layer; keep MCAN hardware config ownership in the CAN adapter.
- If C unit-test infrastructure for the app/CAN adapter is weaker than expected, document that clearly and lean harder on Python tests plus hardware integration tests instead of skipping verification silently.
