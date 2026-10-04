"""
Checks for how the dashboard turns incoming messages into readings.
No hardware, network, or browser needed. Run from the dashboard folder:

    python -m unittest discover -s tests -v

Worth running after any edit to normalize_reading(), process_reading(),
or json_from_serial_line() in app.py, for example when the final JSON
field names get agreed.
"""

import builtins
import json
import math
import os
import random
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import app as dashboard  # noqa: E402

HEALTH = {"state": 3, "active_faults": 0, "latched_faults": 0, "sample_time_ms": 123452,
          "age_ms": 4, "sequence": 416, "error_count": 0, "has_sample": True}

# Same shape as draft_sensor_message.json from the sensor component.
DRAFT = {
    "schema_version": 0, "contract_status": "draft", "device_uptime_ms": 123456,
    "sequence": 42, "valid": True,
    "imu": {"valid": True, "pitch_deg": -2.35, "roll_deg": 0.7, "health": HEALTH},
    "light": {"valid": True, "pitch_error_norm": 0.1042, "roll_error_norm": -0.0317,
              "top_left_adc": 1820, "top_right_adc": 1760, "bottom_left_adc": 1510,
              "bottom_right_adc": 1580, "health": HEALTH},
    "power": {"valid": True, "load_voltage_v": 12.48, "current_a": 0.73,
              "power_w": 9.1104, "shunt_voltage_mv": 73.0, "health": HEALTH},
}


class Capture(unittest.TestCase):
    """Replaces the real socket emit so tests can see what would reach a browser."""

    def setUp(self):
        self.emitted = []
        self._emit = dashboard.socketio.emit
        self._print = builtins.print
        dashboard.socketio.emit = lambda event, data: self.emitted.append((event, data))
        builtins.print = lambda *a, **k: None
        dashboard._last_drop_warning = 0.0

    def tearDown(self):
        dashboard.socketio.emit = self._emit
        builtins.print = self._print

    def readings(self):
        return [data for event, data in self.emitted if event == "sensor_update"]


class NormalizeTests(unittest.TestCase):
    def test_flat(self):
        self.assertEqual(dashboard.normalize_reading({"pitch": 1, "roll": 2, "power": 3}),
                         {"pitch": 1.0, "roll": 2.0, "power": 3.0})

    def test_nested_draft_schema(self):
        self.assertEqual(dashboard.normalize_reading(DRAFT),
                         {"pitch": -2.35, "roll": 0.7, "power": 9.1104})

    def test_partial_flat_message_keeps_only_what_it_has(self):
        self.assertEqual(dashboard.normalize_reading({"power": 3.5}), {"power": 3.5})

    def test_invalid_sensor_comes_through_as_null(self):
        invalid = {"imu": {"valid": False, "pitch_deg": None, "roll_deg": None},
                   "power": {"valid": False, "power_w": None}}
        self.assertEqual(dashboard.normalize_reading(invalid),
                         {"pitch": None, "roll": None, "power": None})

    def test_values_that_are_not_usable_numbers_become_null(self):
        bad = {"pitch": "5.2", "roll": True, "power": float("nan")}
        self.assertEqual(dashboard.normalize_reading(bad),
                         {"pitch": None, "roll": None, "power": None})
        self.assertIsNone(dashboard.clean_number(float("inf")))
        self.assertIsNone(dashboard.clean_number(10 ** 400))


class ProcessReadingTests(Capture):
    def test_accepts_dict_str_bytes_and_bytearray(self):
        text = '{"pitch": 1, "roll": 2, "power": 3}'
        for message in ({"pitch": 1, "roll": 2, "power": 3}, text, text.encode(), bytearray(text.encode())):
            self.emitted.clear()
            dashboard.process_reading(message)
            self.assertEqual(self.readings(), [{"pitch": 1.0, "roll": 2.0, "power": 3.0}])

    def test_array_of_readings(self):
        dashboard.process_reading('[{"pitch": 1}, {"pitch": 2}]')
        self.assertEqual(self.readings(), [{"pitch": 1.0}, {"pitch": 2.0}])

    def test_bad_input_is_dropped_without_raising(self):
        for message in (b"\xff\xfe junk", '{"pitch": 1,', "42", "[1, 2]", "", "   \r\n", b"",
                        None, 7, '{"temperature": 20}', "[" * 100000, b"9" * 5000):
            self.emitted.clear()
            dashboard.process_reading(message)
            self.assertEqual(self.readings(), [], f"should not emit for {message!r:.40}")

    def test_nan_token_never_reaches_the_browser(self):
        dashboard.process_reading(b'{"pitch": NaN, "roll": 2}')
        self.assertEqual(self.readings(), [{"pitch": None, "roll": 2.0}])
        json.dumps(self.readings()[0], allow_nan=False)  # strict JSON must succeed


class SerialLineTests(unittest.TestCase):
    def test_lines_that_carry_json(self):
        body = b'{"pitch":1,"roll":2}'
        for line in (body + b"\r\n",
                     b"sensor_json," + body + b"\r\n",                       # sensor component's labeled line
                     b"I (1234) telemetry: " + body + b"\r\n",               # sent through ESP_LOGI
                     b"\x1b[0;32mI (1234) telemetry: " + body + b"\x1b[0m\r\n"):
            self.assertEqual(dashboard.json_from_serial_line(line), body, line)

    def test_lines_that_do_not(self):
        for line in (b"ESP-ROM:esp32s3-20210327\r\n", b"I (321) main: boot complete\r\n",
                     b"I (321) main: heap {free}\r\n", b"  imu_values: pitch_deg=1.25\r\n",
                     b"sensor_json_error=2 required_buffer=2048\r\n", b"\r\n", b"42\r\n",
                     b"[WARN] low battery\r\n"):
            self.assertIsNone(dashboard.json_from_serial_line(line), line)


class RandomInputTests(Capture):
    def test_random_input_never_raises_and_output_is_browser_safe(self):
        rng = random.Random(7)
        template = json.dumps(DRAFT).encode()
        for i in range(3000):
            if i % 3 == 0:
                message = bytes(rng.getrandbits(8) for _ in range(rng.randint(0, 100)))
            elif i % 3 == 1:
                mutated = bytearray(template)
                for _ in range(rng.randint(1, 5)):
                    mutated[rng.randrange(len(mutated))] = rng.getrandbits(8)
                message = bytes(mutated[:rng.randint(1, len(mutated))])
            else:
                message = bytearray(bytes(rng.getrandbits(8) for _ in range(rng.randint(0, 100))))
            self.emitted.clear()
            dashboard.process_reading(message)
            for reading in self.readings():
                self.assertLessEqual(set(reading), {"pitch", "roll", "power"})
                for value in reading.values():
                    self.assertTrue(value is None or (isinstance(value, float) and math.isfinite(value)))


if __name__ == "__main__":
    unittest.main()
