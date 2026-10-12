"""
Checks for the serial helpers: line splitting, picking the board's port,
and turning port errors into readable messages. No hardware needed.

    python -m unittest discover -s tests -v
"""

import os
import sys
import unittest
from types import SimpleNamespace

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import app as dashboard  # noqa: E402


def port(device, vid, description="USB device"):
    return SimpleNamespace(device=device, vid=vid, description=description)


class SerialLinesTests(unittest.TestCase):
    def test_lines_split_across_chunks(self):
        lines = dashboard.SerialLines()
        self.assertEqual(lines.feed(b'{"pitch":1'), [])
        self.assertEqual(lines.feed(b'}\r\n{"roll":2}\n{"po'), [b'{"pitch":1}\r', b'{"roll":2}'])
        self.assertEqual(lines.feed(b'wer":3}\n'), [b'{"power":3}'])

    def test_runaway_line_is_dropped(self):
        lines = dashboard.SerialLines(max_line=100)
        lines.feed(b"x" * 500)
        self.assertEqual(lines.feed(b'tail\n{"pitch":1}\n'), [b"tail", b'{"pitch":1}'])

    def test_crlf_lines_still_parse(self):
        line = dashboard.SerialLines().feed(b'{"pitch":1}\r\n')[0]
        self.assertEqual(dashboard.json_from_serial_line(line), b'{"pitch":1}')


class PickPortTests(unittest.TestCase):
    def test_single_espressif_board(self):
        ports = [port("/dev/cu.Bluetooth-Incoming-Port", None), port("/dev/cu.usbmodem1101", 0x303A)]
        self.assertEqual(dashboard.pick_serial_port(ports)[0], "/dev/cu.usbmodem1101")

    def test_single_usb_serial_chip(self):
        self.assertEqual(dashboard.pick_serial_port([port("COM5", 0x10C4)])[0], "COM5")

    def test_board_with_both_usb_ports_plugged_in_prefers_native(self):
        ports = [port("/dev/ttyUSB0", 0x10C4), port("/dev/ttyACM0", 0x303A)]
        self.assertEqual(dashboard.pick_serial_port(ports)[0], "/dev/ttyACM0")

    def test_two_boards_is_ambiguous(self):
        found, why = dashboard.pick_serial_port([port("COM3", 0x303A), port("COM4", 0x303A)])
        self.assertIsNone(found)
        self.assertIn("several possible boards", why)

    def test_unknown_chip_used_when_it_is_the_only_usb_port(self):
        self.assertEqual(dashboard.pick_serial_port([port("COM7", 0x2341)])[0], "COM7")

    def test_nothing_plugged_in(self):
        found, why = dashboard.pick_serial_port([port("/dev/ttyS0", None)])
        self.assertIsNone(found)
        self.assertIn("no ESP32 board found", why)


class ErrorMessageTests(unittest.TestCase):
    def test_port_in_use(self):
        msg = dashboard.explain_serial_error(
            Exception("Could not exclusively lock port /dev/ttyACM0: [Errno 11] Resource temporarily unavailable"),
            "/dev/ttyACM0")
        self.assertIn("another program", msg)

    def test_unplugged(self):
        msg = dashboard.explain_serial_error(
            Exception("[Errno 2] could not open port /dev/ttyACM0: [Errno 2] No such file or directory"),
            "/dev/ttyACM0")
        self.assertIn("not there any more", msg)

    def test_disconnect_while_reading(self):
        msg = dashboard.explain_serial_error(Exception(
            "device reports readiness to read but returned no data (device disconnected or multiple access on port?)"),
            "/dev/ttyACM0")
        self.assertIn("unplugged or reset", msg)

    def test_other_errors_pass_through(self):
        self.assertEqual(dashboard.explain_serial_error(Exception("something odd"), "COM3"), "something odd")


    @unittest.skipUnless(sys.platform.startswith("linux"), "Linux permission hint")
    def test_permission_hint_names_the_group_that_owns_the_port(self):
        import grp
        import tempfile
        with tempfile.NamedTemporaryFile() as fake_port:
            group = grp.getgrgid(os.stat(fake_port.name).st_gid).gr_name
            message = dashboard.explain_serial_error(PermissionError(13, "Permission denied"), fake_port.name)
        self.assertIn("usermod -aG " + group + " ", message)

    def test_port_group_falls_back_to_dialout(self):
        self.assertEqual(dashboard.port_group("/dev/not-a-real-port"), "dialout")


if __name__ == "__main__":
    unittest.main()
