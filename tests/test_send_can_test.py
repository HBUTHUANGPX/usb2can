import pathlib
import sys
import unittest


TOOLS_DIR = pathlib.Path(__file__).resolve().parents[1] / "tools"
if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))

import send_can_test


class BuildPacketTest(unittest.TestCase):
    def test_build_protocol_frame_matches_expected_bytes(self):
        frame = send_can_test.build_protocol_frame(0x123, bytes([0x11, 0x22, 0x33, 0x44]))

        self.assertEqual(
            frame,
            bytes([0xA5, 0x01, 0x07, 0x00, 0x2B, 0x2F, 0x56, 0x23, 0x01, 0x04, 0x11, 0x22, 0x33, 0x44]),
        )

    def test_build_batched_frames_repeats_frame_count_times(self):
        frame = bytes([0xAA, 0xBB, 0xCC])

        batched = send_can_test.build_batched_frames(frame, 4)

        self.assertEqual(batched, frame * 4)

    def test_build_set_mode_frame_matches_expected_bytes(self):
        frame = send_can_test.build_set_mode_frame(send_can_test.MODE_CANFD_STD)

        self.assertEqual(frame, bytes([0xA5, 0x12, 0x01, 0x00, 0x1F, 0xD1, 0xF1, 0x01]))

    def test_build_get_mode_frame_matches_expected_bytes(self):
        frame = send_can_test.build_get_mode_frame()

        self.assertEqual(frame, bytes([0xA5, 0x10, 0x00, 0x00, 0x67, 0xFF, 0xFF]))

    def test_build_get_capability_frame_matches_expected_bytes(self):
        frame = send_can_test.build_get_capability_frame()

        self.assertEqual(frame, bytes([0xA5, 0x14, 0x00, 0x00, 0x4E, 0xFF, 0xFF]))

    def test_build_canfd_protocol_frame_matches_expected_bytes(self):
        payload = bytes(range(12))

        frame = send_can_test.build_canfd_protocol_frame(0x123, payload)

        self.assertEqual(
            frame,
            bytes(
                [
                    0xA5,
                    0x03,
                    0x0F,
                    0x00,
                    0x90,
                    0xEE,
                    0xB7,
                    0x23,
                    0x01,
                    0x0C,
                    0x00,
                    0x01,
                    0x02,
                    0x03,
                    0x04,
                    0x05,
                    0x06,
                    0x07,
                    0x08,
                    0x09,
                    0x0A,
                    0x0B,
                ]
            ),
        )

    def test_build_canfd_protocol_frame_rejects_non_canonical_length(self):
        with self.assertRaises(ValueError):
            send_can_test.build_canfd_protocol_frame(0x123, bytes(range(15)))


class ParseArgsTest(unittest.TestCase):
    def test_parse_args_uses_expected_defaults(self):
        args = send_can_test.parse_args([])

        self.assertEqual(args.port, "/dev/ttyACM0")
        self.assertEqual(args.can_id, "0x123")
        self.assertEqual(args.data, "11 22 33 44")
        self.assertEqual(args.mode, "can2")
        self.assertEqual(args.count, 0)
        self.assertAlmostEqual(args.interval, 0.1)

    def test_parse_args_accepts_finite_burst_mode(self):
        args = send_can_test.parse_args(["--count", "10", "--interval", "0.01"])

        self.assertEqual(args.count, 10)
        self.assertAlmostEqual(args.interval, 0.01)

    def test_parse_args_accepts_pack_count_for_finite_mode(self):
        args = send_can_test.parse_args(["--count", "10", "--pack-count"])

        self.assertEqual(args.count, 10)
        self.assertTrue(args.pack_count)

    def test_parse_args_rejects_pack_count_in_continuous_mode(self):
        with self.assertRaises(SystemExit):
            send_can_test.parse_args(["--pack-count"])

    def test_parse_args_accepts_canfd_mode(self):
        args = send_can_test.parse_args(["--mode", "canfd", "--data", "00 01 02 03 04 05 06 07 08 09 0A 0B"])

        self.assertEqual(args.mode, "canfd")

    def test_parse_args_accepts_canfd_brs_mode(self):
        args = send_can_test.parse_args(["--mode", "canfd-brs", "--data", "00 01 02 03 04 05 06 07 08 09 0A 0B"])

        self.assertEqual(args.mode, "canfd-brs")

    def test_parse_args_accepts_query_mode(self):
        args = send_can_test.parse_args(["--query", "get-mode", "--read-response"])

        self.assertEqual(args.query, "get-mode")
        self.assertTrue(args.read_response)

    def test_parse_args_accepts_set_mode_only(self):
        args = send_can_test.parse_args(["--mode", "canfd", "--set-mode-only"])

        self.assertTrue(args.set_mode_only)

    def test_should_send_mode_select_is_false_for_query(self):
        args = send_can_test.parse_args(["--query", "get-mode"])

        self.assertFalse(send_can_test.should_send_mode_select(args))

    def test_should_send_mode_select_is_true_for_data_send(self):
        args = send_can_test.parse_args(["--mode", "canfd"])

        self.assertTrue(send_can_test.should_send_mode_select(args))


if __name__ == "__main__":
    unittest.main()
