import pathlib
import sys
import unittest


TOOLS_DIR = pathlib.Path(__file__).resolve().parents[1] / "tools"
if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))

import recv_can_test


class ParsePacketTest(unittest.TestCase):
    def test_parse_can_rx_report_packet(self):
        raw_frame = bytes(
            [0xA5, 0x02, 0x07, 0x00, 0xE1, 0x2F, 0x56, 0x23, 0x01, 0x04, 0x11, 0x22, 0x33, 0x44]
        )

        packet = recv_can_test.parse_protocol_frame(raw_frame)

        self.assertEqual(packet["cmd"], recv_can_test.CMD_CAN_RX_REPORT)
        self.assertEqual(packet["len"], 7)
        self.assertEqual(packet["data"], bytes([0x23, 0x01, 0x04, 0x11, 0x22, 0x33, 0x44]))

    def test_decode_can_frame_from_report(self):
        packet = {
            "cmd": recv_can_test.CMD_CAN_RX_REPORT,
            "data": bytes([0x23, 0x01, 0x04, 0x11, 0x22, 0x33, 0x44]),
            "raw_frame": bytes(
                [0xA5, 0x02, 0x07, 0x00, 0xE1, 0x2F, 0x56, 0x23, 0x01, 0x04, 0x11, 0x22, 0x33, 0x44]
            ),
        }

        decoded = recv_can_test.decode_packet(packet)

        self.assertEqual(decoded["kind"], "can_rx")
        self.assertEqual(decoded["can_id"], 0x123)
        self.assertEqual(decoded["dlc"], 4)
        self.assertEqual(decoded["payload"], bytes([0x11, 0x22, 0x33, 0x44]))


class StreamParserTest(unittest.TestCase):
    def test_stream_parser_reassembles_frame_across_chunks(self):
        parser = recv_can_test.ProtocolStreamParser()
        raw_frame = bytes(
            [0xA5, 0x02, 0x07, 0x00, 0xE1, 0x2F, 0x56, 0x23, 0x01, 0x04, 0x11, 0x22, 0x33, 0x44]
        )

        first = parser.push(raw_frame[:5])
        second = parser.push(raw_frame[5:])

        self.assertEqual(first, [])
        self.assertEqual(len(second), 1)
        self.assertEqual(second[0]["cmd"], recv_can_test.CMD_CAN_RX_REPORT)


class FormatMessageTest(unittest.TestCase):
    def test_format_can_rx_message_contains_expected_fields(self):
        message = recv_can_test.format_decoded_message(
            {
                "kind": "can_rx",
                "can_id": 0x123,
                "dlc": 4,
                "payload": bytes([0x11, 0x22, 0x33, 0x44]),
                "raw_frame": bytes(
                    [0xA5, 0x02, 0x07, 0x00, 0xE1, 0x2F, 0x56, 0x23, 0x01, 0x04, 0x11, 0x22, 0x33, 0x44]
                ),
            }
        )

        self.assertIn("CAN_RX", message)
        self.assertIn("0x123", message)
        self.assertIn("11 22 33 44", message)


if __name__ == "__main__":
    unittest.main()
