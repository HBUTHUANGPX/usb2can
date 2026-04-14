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


class ParseArgsTest(unittest.TestCase):
    def test_parse_args_uses_expected_defaults(self):
        args = send_can_test.parse_args([])

        self.assertEqual(args.port, "/dev/ttyACM0")
        self.assertEqual(args.can_id, "0x123")
        self.assertEqual(args.data, "11 22 33 44")
        self.assertEqual(args.count, 1)
        self.assertAlmostEqual(args.interval, 0.1)


if __name__ == "__main__":
    unittest.main()
