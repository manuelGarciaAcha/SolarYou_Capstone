"""
Team 11 - Active Stabilization and Solar Tracking Platform
Dashboard server: serves the live view and pushes sensor data to any
connected browser over Socket.IO.

Pick the data source at startup instead of editing code:
    python app.py --source mock      (default)
    python app.py --source serial
    python app.py --source wifi
    python app.py --source ble

Per-machine connection details (serial port, WiFi URL, BLE device name)
live in config.json, NOT in this file -- see config.example.json. Each
teammate keeps their own config.json locally; it's gitignored since a
Mac's serial port path looks nothing like a Windows one.
"""

import argparse
import json
import math
import os
import random
import threading
import time

from flask import Flask, render_template
from flask_socketio import SocketIO

app = Flask(__name__)
app.config["SECRET_KEY"] = "dev"
socketio = SocketIO(app)

UPDATE_INTERVAL_SECONDS = 1.0
CONFIG_PATH = os.path.join(os.path.dirname(__file__), "config.json")


def load_config():
    """
    Per-machine settings (serial port, WiFi URL, BLE device name). Falls
    back to empty values if config.json doesn't exist yet -- the relevant
    data-source function will raise a clear error if something's missing
    rather than failing silently.
    """
    if os.path.exists(CONFIG_PATH):
        with open(CONFIG_PATH) as f:
            return json.load(f)
    return {}


# ---- shared data-source handling ----

connection_state = {"status": "connecting"}


def set_connection_status(status, message=None):
    """
    Tracks the current connection state and broadcasts it. Plain
    socketio.emit() only reaches clients connected *right now* -- a
    browser that loads the page after the ESP32 already connected would
    never see that "connected" event and the badge would stay stuck on
    whatever the HTML's initial text was, forever. Keeping the latest
    state here means a newly-connecting browser can be caught up
    immediately (see browser_connected() below), not just clients that
    happened to already be watching.
    """
    global connection_state
    connection_state = {"status": status}
    if message:
        connection_state["message"] = message
    socketio.emit("connection_status", connection_state)


@socketio.on("connect")
def browser_connected():
    """Catch up a newly-connected browser with whatever the state already is."""
    socketio.emit("connection_status", connection_state)


def run_data_source(fn, config):
    """
    Wraps whichever data-source function is running in the background
    thread. Exceptions raised inside a thread don't propagate to the main
    thread or crash Flask -- they just print a traceback and the thread
    quietly dies, with nothing on the dashboard showing anything went
    wrong. This makes failures visible in both places: the terminal, and
    the dashboard itself, since that's what's actually being watched
    during a demo, not the terminal.
    """
    try:
        set_connection_status("connecting")
        fn(config)
    except Exception as exc:
        print(f"[ERROR] Data source failed: {exc}", flush=True)
        set_connection_status("error", str(exc))


def normalize_reading(reading):
    """
    The one seam between whatever the ESP32 actually sends and what the
    dashboard expects. dashboard.js only ever knows about a flat
    {pitch, roll, power} shape -- it doesn't know or care whether the
    real firmware schema ends up flat (like the mock data) or grouped by
    sensor (like Jennifer's current draft: imu.pitch_deg, imu.roll_deg,
    power.power_w, each with its own health/fault block). If the real
    schema changes, this is the only function that needs to change.

    Always returns all three keys, using None for anything missing,
    rather than sometimes omitting a key -- sensor groups update at
    different rates (IMU ~50Hz, power ~5Hz per the timing already
    described in Written Report 1), so a given message legitimately may
    not carry every group every time. A consistent shape means the
    frontend always knows what keys to expect, rather than needing to
    guard against a key not existing at all.

    Also the natural place to eventually: reject a reading whose health
    block reports a fault, and pull out whatever window of recent values
    TinyML ends up needing -- both read from the raw nested reading
    before it gets flattened down to what the dashboard displays.
    """
    if "pitch" in reading:
        return reading  # already flat (mock data, or a finalized flat schema)

    # Jennifer's nested draft schema: grouped by sensor, each with its
    # own health/fault block
    return {
        "pitch": reading.get("imu", {}).get("pitch_deg"),
        "roll": reading.get("imu", {}).get("roll_deg"),
        "power": reading.get("power", {}).get("power_w"),
    }


def process_reading(message):
    """
    Single entry point for a reading from any data source, regardless of
    transport. Accepts a dict (mock data, native Python already), raw
    bytes (serial/ble hand over undecoded bytes), or a string (websocket
    already decodes text frames on its own). This is also where future
    logic goes -- once, not once per source:
      - validate schema
      - normalize units
      - reject stale/invalid sensor values
      - calculate any derived dashboard fields
    """
    if isinstance(message, dict):
        reading = message
    else:
        if isinstance(message, bytes):
            try:
                message = message.decode("utf-8")
            except UnicodeDecodeError:
                return  # corrupted/partial bytes, not our problem to crash over
        try:
            reading = json.loads(message)
        except json.JSONDecodeError:
            return  # skip malformed/partial messages

    socketio.emit("sensor_update", normalize_reading(reading))


# ---- mock data ----

def generate_mock_reading(t):
    decay = math.exp(-0.05 * t)
    pitch = 8 * decay * math.sin(0.5 * t) + random.uniform(-0.3, 0.3)
    roll = 6 * decay * math.cos(0.4 * t) + random.uniform(-0.3, 0.3)
    power = 1.2 + 0.1 * math.sin(0.1 * t) + random.uniform(-0.05, 0.05)
    return {"pitch": round(pitch, 2), "roll": round(roll, 2), "power": round(power, 2)}


def mock_data_loop(config):
    set_connection_status("connected")
    t = 0
    while True:
        process_reading(generate_mock_reading(t))  # dict, passed straight through
        t += UPDATE_INTERVAL_SECONDS
        time.sleep(UPDATE_INTERVAL_SECONDS)


# ---- serial (needs: pip install pyserial) ----

def serial_data_loop(config):
    import serial

    port = config.get("serial_port")
    if not port:
        raise ValueError('config.json is missing "serial_port"')

    ser = serial.Serial(port, config.get("serial_baud", 115200))
    set_connection_status("connected")
    while True:
        line = ser.readline().strip()
        if line:
            process_reading(line)


# ---- WiFi / WebSocket (needs: pip install websocket-client) ----

def websocket_data_loop(config):
    import websocket

    url = config.get("wifi_url")
    if not url:
        raise ValueError('config.json is missing "wifi_url"')

    while True:
        try:
            ws = websocket.WebSocket()
            ws.connect(url)
            print("WiFi connected")
            set_connection_status("connected")

            while True:
                process_reading(ws.recv())

        except (websocket.WebSocketException, OSError, ConnectionError) as exc:
            print(f"WiFi connection lost: {exc}")
            print("Retrying in 2 seconds...")
            set_connection_status("reconnecting", str(exc))
            time.sleep(2)


# ---- Bluetooth LE (needs: pip install bleak) ----
#
# Discover by advertised name instead of relying on a hardcoded BLE
# address. macOS/CoreBluetooth does not expose a peripheral's real BLE
# MAC address to apps at all, so name-based discovery is what keeps this
# working the same way on macOS and Windows.

def ble_data_loop(config):
    import asyncio
    from bleak import BleakScanner, BleakClient

    device_name = config.get("ble_name")
    characteristic_uuid = config.get("ble_characteristic")
    if not device_name or not characteristic_uuid:
        raise ValueError('config.json is missing "ble_name" or "ble_characteristic"')

    async def handle_notify(_, data):
        process_reading(data)

    async def run():
        while True:
            try:
                print(f"Scanning for BLE device '{device_name}'...")
                device = await BleakScanner.find_device_by_name(device_name, timeout=10.0)
                if device is None:
                    raise RuntimeError(f"Could not find BLE device '{device_name}'")

                # sleep() alone would never notice a real disconnect --
                # the callback is what actually signals one, so the
                # retry loop below has something to wait on.
                disconnected = asyncio.Event()

                def on_disconnect(_client):
                    disconnected.set()

                async with BleakClient(device, disconnected_callback=on_disconnect) as client:
                    await client.start_notify(characteristic_uuid, handle_notify)
                    print("BLE connected")
                    set_connection_status("connected")
                    await disconnected.wait()
                    print("BLE disconnected")

            except Exception as exc:
                print(f"BLE connection lost/failed: {exc}")
                set_connection_status("reconnecting", str(exc))

            print("Retrying in 2 seconds...")
            await asyncio.sleep(2)

    asyncio.run(run())


DATA_SOURCES = {
    "mock": mock_data_loop,
    "serial": serial_data_loop,
    "wifi": websocket_data_loop,
    "ble": ble_data_loop,
}


@app.route("/")
def index():
    return render_template("index.html")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--source", choices=DATA_SOURCES.keys(), default="mock",
        help="Where sensor data comes from (default: mock)",
    )
    args = parser.parse_args()

    config = load_config()
    data_loop = DATA_SOURCES[args.source]
    threading.Thread(target=run_data_source, args=(data_loop, config), daemon=True).start()

    # use_reloader=False: the reloader spawns a second process that runs
    # this whole file again, including the thread-start line above --
    # without this, debug mode silently opens two BLE/serial/WiFi
    # connections at once. Also just correct for this app generally:
    # auto-restart-on-save would kill and reopen a live hardware
    # connection every time a file is saved, which isn't desirable here
    # regardless of debug settings.
    socketio.run(app, debug=True, use_reloader=False, host="0.0.0.0", port=5001,
                 allow_unsafe_werkzeug=True)
