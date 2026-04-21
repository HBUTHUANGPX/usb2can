# USB2CAN CANFD BRS Extended Frame Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add transparent CAN FD BRS extended-frame support while preserving existing standard-frame behavior.

**Architecture:** The USB private protocol gets two new data-plane commands for CAN FD extended TX/RX reports. Firmware keeps the same mode model and task queues, but carries an `is_extended_id` flag through bridge, app, and MCAN adapter layers. Host tools expose this as `--frame-format ext`.

**Tech Stack:** C, HPM SDK MCAN driver, FreeRTOS queues, Python `argparse`, Python `unittest`.

---

### Task 1: Lock Protocol Behavior With Tests

**Files:**
- Modify: `tests/usb2can_bridge_test.c`
- Modify: `tests/usb2can_protocol_test.c`
- Modify: `tests/test_send_can_test.py`
- Modify: `tests/test_recv_can_test.py`
- Modify: `tests/test_run_stress_test.py`

- [ ] Add failing tests for extended CAN FD payload round-trip and invalid IDs.
- [ ] Add failing tests for `0x05/0x06` command constants.
- [ ] Add failing Python tests for extended TX encoding, RX decoding, CLI validation, and stress scenario command generation.
- [ ] Run the focused tests and confirm they fail because implementation is missing.

### Task 2: Implement Firmware Protocol And MCAN Path

**Files:**
- Modify: `inc/usb2can_types.h`
- Modify: `inc/usb2can_bridge.h`
- Modify: `src/usb2can_bridge.c`
- Modify: `inc/usb2can_can.h`
- Modify: `src/usb2can_can.c`
- Modify: `src/usb2can_app.c`

- [ ] Add extended command constants and frame type.
- [ ] Add bridge parse/serialize helpers for `u32 can_id + u8 len + payload`.
- [ ] Add internal `is_extended_id` transport flag.
- [ ] Send standard and extended CAN FD frames through the correct MCAN ID fields.
- [ ] Route received extended CAN FD frames to `CMD_CANFD_EXT_RX_REPORT`.

### Task 3: Implement Host Tool Support

**Files:**
- Modify: `tools/send_can_test.py`
- Modify: `tools/recv_can_test.py`
- Modify: `tools/run_stress_test.py`

- [ ] Add `--frame-format` and extended ID validation.
- [ ] Build `CMD_CANFD_EXT_TX` payloads for `canfd-brs` extended frames.
- [ ] Decode and print `CMD_CANFD_EXT_RX_REPORT`.
- [ ] Add `canfd-brs-ext-burst` to the stress runner.

### Task 4: Update Documentation And Verify

**Files:**
- Modify: `README.md`
- Modify: `README_zh.md`
- Modify: `docs/2026-04-17-usb2can-stress-test-plan.md`
- Modify: `docs/2026-04-17-usb2can-stress-test-plan-en.md`

- [ ] Document standard vs extended CAN FD command payloads in English and Chinese.
- [ ] Document the hardware test command.
- [ ] Run C host tests and Python unittest suite.
- [ ] Commit the completed change set.
