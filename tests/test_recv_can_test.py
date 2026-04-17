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

    def test_decode_canfd_frame_from_report(self):
        packet = {
            "cmd": recv_can_test.CMD_CANFD_RX_REPORT,
            "data": bytes([0x23, 0x01, 0x0C]) + bytes(range(12)),
            "raw_frame": bytes(
                [0xA5, 0x04, 0x0F, 0x00, 0x80, 0xD7, 0x68, 0x23, 0x01, 0x0C]
                + list(range(12))
            ),
        }

        decoded = recv_can_test.decode_packet(packet)

        self.assertEqual(decoded["kind"], "canfd_rx")
        self.assertEqual(decoded["can_id"], 0x123)
        self.assertEqual(decoded["data_length"], 12)
        self.assertEqual(decoded["payload"], bytes(range(12)))

    def test_decode_get_mode_response(self):
        packet = {
            "cmd": recv_can_test.CMD_GET_MODE_RSP,
            "data": bytes([recv_can_test.MODE_CANFD_STD_BRS]),
            "raw_frame": bytes([0xA5, 0x11, 0x01, 0x00, 0x6D, 0x00, 0x41, 0x02]),
        }

        decoded = recv_can_test.decode_packet(packet)

        self.assertEqual(decoded["kind"], "get_mode_rsp")
        self.assertEqual(decoded["mode"], recv_can_test.MODE_CANFD_STD_BRS)

    def test_decode_set_mode_response(self):
        packet = {
            "cmd": recv_can_test.CMD_SET_MODE_RSP,
            "data": bytes([0x00, recv_can_test.MODE_CANFD_STD]),
            "raw_frame": bytes([0xA5, 0x13, 0x02, 0x00, 0x74, 0x2E, 0x0D, 0x00, 0x01]),
        }

        decoded = recv_can_test.decode_packet(packet)

        self.assertEqual(decoded["kind"], "set_mode_rsp")
        self.assertEqual(decoded["status"], 0x00)
        self.assertEqual(decoded["mode"], recv_can_test.MODE_CANFD_STD)

    def test_decode_get_capability_response(self):
        packet = {
            "cmd": recv_can_test.CMD_GET_CAPABILITY_RSP,
            "data": bytes([0x07, 0x00, 0x40]),
            "raw_frame": bytes([0xA5, 0x15, 0x03, 0x00, 0x29, 0x6A, 0x34, 0x07, 0x00, 0x40]),
        }

        decoded = recv_can_test.decode_packet(packet)

        self.assertEqual(decoded["kind"], "get_capability_rsp")
        self.assertEqual(decoded["mode_bitmap"], 0x0007)
        self.assertEqual(decoded["max_canfd_length"], 64)


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

    def test_format_canfd_rx_message_contains_expected_fields(self):
        message = recv_can_test.format_decoded_message(
            {
                "kind": "canfd_rx",
                "can_id": 0x123,
                "data_length": 12,
                "payload": bytes(range(12)),
                "raw_frame": bytes(
                    [0xA5, 0x04, 0x0F, 0x00, 0x80, 0xD7, 0x68, 0x23, 0x01, 0x0C]
                    + list(range(12))
                ),
            }
        )

        self.assertIn("CANFD_RX", message)
        self.assertIn("len=12", message)
        self.assertIn("00 01 02 03 04 05 06 07 08 09 0A 0B", message)

    def test_format_set_mode_response_contains_expected_fields(self):
        message = recv_can_test.format_decoded_message(
            {
                "kind": "set_mode_rsp",
                "status": 0x00,
                "mode": recv_can_test.MODE_CANFD_STD,
                "raw_frame": bytes([0xA5, 0x13, 0x02, 0x00, 0x74, 0x2E, 0x0D, 0x00, 0x01]),
            }
        )

        self.assertIn("SET_MODE_RSP", message)
        self.assertIn("status=0x00", message)
        self.assertIn("mode=0x01", message)


if __name__ == "__main__":
    unittest.main()
