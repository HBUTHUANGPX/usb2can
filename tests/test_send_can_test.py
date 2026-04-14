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


class ParseArgsTest(unittest.TestCase):
    def test_parse_args_uses_expected_defaults(self):
        args = send_can_test.parse_args([])

        self.assertEqual(args.port, "/dev/ttyACM0")
        self.assertEqual(args.can_id, "0x123")
        self.assertEqual(args.data, "11 22 33 44")
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


if __name__ == "__main__":
    unittest.main()
