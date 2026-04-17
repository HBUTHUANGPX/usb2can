import pathlib
import sys
import unittest


TOOLS_DIR = pathlib.Path(__file__).resolve().parents[1] / "tools"
if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))

import run_stress_test


class GeneratePayloadTest(unittest.TestCase):
    def test_generate_hex_payload_for_12_bytes(self):
        self.assertEqual(
            run_stress_test.generate_hex_payload(12),
            "00 01 02 03 04 05 06 07 08 09 0A 0B",
        )

    def test_generate_hex_payload_rejects_invalid_canfd_length(self):
        with self.assertRaises(ValueError):
            run_stress_test.generate_hex_payload(15)


class BuildCommandTest(unittest.TestCase):
    def test_build_mode_switch_commands_for_one_loop(self):
        commands = run_stress_test.build_mode_switch_commands("/dev/ttyACM0", 115200, 1)

        self.assertEqual(len(commands), 6)
        self.assertIn("--set-mode-only", commands[0])
        self.assertEqual(commands[0][commands[0].index("--mode") + 1], "canfd")
        self.assertIn("--query", commands[1])
        self.assertEqual(commands[1][commands[1].index("--query") + 1], "get-mode")

    def test_build_burst_command_for_canfd_brs_uses_64_bytes(self):
        command = run_stress_test.build_burst_command("/dev/ttyACM0", 115200, "canfd-brs", 64, 50)

        self.assertEqual(command[command.index("--mode") + 1], "canfd-brs")
        self.assertEqual(command[command.index("--count") + 1], "50")
        self.assertEqual(len(command[command.index("--data") + 1].split()), 64)


class ParseArgsTest(unittest.TestCase):
    def test_parse_args_uses_expected_defaults(self):
        args = run_stress_test.parse_args([])

        self.assertEqual(args.port, "/dev/ttyACM0")
        self.assertEqual(args.switch_loops, 10)
        self.assertEqual(args.burst_count, 50)
        self.assertEqual(
            args.scenarios,
            ["mode-switch", "can2-burst", "canfd-burst", "canfd-brs-burst"],
        )

    def test_iter_commands_honors_selected_scenarios(self):
        args = run_stress_test.parse_args(["--switch-loops", "2", "--scenarios", "mode-switch", "canfd-burst"])

        plan = run_stress_test.iter_commands(args)

        self.assertEqual(len(plan), 13)
        self.assertEqual(plan[-1][0], "canfd-burst")


if __name__ == "__main__":
    unittest.main()
