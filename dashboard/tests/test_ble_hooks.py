"""
Checks for the dashboard side of the BLE connection: the send-rate control
only appears with --source ble, and the BLE status messages reach the
terminal. The BLE receiver itself (ble_transport.py) has its own tests.
No hardware or Bluetooth adapter needed.

    python -m unittest discover -s tests -v
"""

import builtins
import os
import sys
import types
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import app as dashboard  # noqa: E402


class RateControlVisibilityTests(unittest.TestCase):
    def page_for(self, source):
        dashboard.app.config["DATA_SOURCE"] = source
        return dashboard.app.test_client().get("/").get_data(as_text=True)

    def tearDown(self):
        dashboard.app.config.pop("DATA_SOURCE", None)

    def test_hidden_for_other_sources(self):
        for source in ("mock", "serial", "wifi"):
            self.assertIn('<div class="rate-control" hidden>', self.page_for(source), source)

    def test_shown_for_ble(self):
        page = self.page_for("ble")
        self.assertIn('<div class="rate-control">', page)
        self.assertIn('id="telemetryRate"', page)


class FakeLink:
    """Stands in for ble_transport.BleLink and reports a scripted set of states."""
    events = []

    def __init__(self, config, on_reading, on_status, on_rate, retry_seconds):
        self.on_status = on_status
        self.on_rate = on_rate

    async def run(self, scanner, client_factory):
        for state, reason in FakeLink.events:
            self.on_status(state, reason)
        self.on_rate({"available": True, "rate_hz": 5, "state": "confirmed"})
        self.on_rate({"available": True, "state": "error", "error": "write timed out"})


class BleTerminalMessageTests(unittest.TestCase):
    def setUp(self):
        self.printed = []
        self.statuses = []
        self._print = builtins.print
        self._link = dashboard.BleLink
        self._emit = dashboard.socketio.emit
        builtins.print = lambda *a, **k: self.printed.append(" ".join(str(x) for x in a))
        dashboard.BleLink = FakeLink
        dashboard.socketio.emit = lambda event, data: self.statuses.append((event, data))
        self._bleak = sys.modules.get("bleak")
        sys.modules["bleak"] = types.SimpleNamespace(BleakScanner=None, BleakClient=None)

    def tearDown(self):
        builtins.print = self._print
        dashboard.BleLink = self._link
        dashboard.socketio.emit = self._emit
        if self._bleak is None:
            sys.modules.pop("bleak", None)
        else:
            sys.modules["bleak"] = self._bleak

    def run_loop(self, events):
        FakeLink.events = events
        dashboard.ble_data_loop({"ble_name": "SolarYou-ASSTP", "ble_characteristic": "x"})
        return "\n".join(self.printed)

    def test_connection_steps_are_printed(self):
        out = self.run_loop([("connecting", "Scanning"), ("connected", None),
                             ("reconnecting", "device disconnected")])
        self.assertIn("Scanning for BLE device 'SolarYou-ASSTP'", out)
        self.assertIn("BLE connected", out)
        self.assertIn("BLE connection lost/failed: device disconnected", out)
        self.assertIn("BLE send rate: 5 updates per second", out)
        self.assertIn("[WARN] BLE rate: write timed out", out)

    def test_missing_characteristic_gets_the_byte_order_hint(self):
        out = self.run_loop([("reconnecting", "Characteristic x was not found!")])
        self.assertIn("NimBLE writes UUID bytes in reverse order", out)
        badge = [data for event, data in self.statuses if event == "connection_status"][-1]
        self.assertIn("reverse order", badge["message"])


if __name__ == "__main__":
    unittest.main()
