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

What a message needs to look like is documented in ESP32_INTEGRATION.md.
"""

import argparse
import json
import math
import os
import random
import re
import socket
import sys
import threading
import time

from flask import Flask, render_template
from flask_socketio import SocketIO, emit

app = Flask(__name__)
app.config["SECRET_KEY"] = "dev"
# async_mode is pinned on purpose. Left on auto, Flask-SocketIO quietly
# switches to eventlet or gevent if either happens to be installed in the
# same environment, and the blocking serial/websocket/BLE reads below
# would then stall the whole server.
socketio = SocketIO(app, async_mode="threading")

UPDATE_INTERVAL_SECONDS = 1.0
RETRY_SECONDS = 2
MAX_MESSAGE_BYTES = 65536   # anything bigger is garbage, not a sensor reading
MAX_BATCH_SIZE = 500        # readings accepted from one message if it's a JSON array
MAX_SERIAL_LINE = 8192
CONFIG_PATH = os.path.join(os.path.dirname(__file__), "config.json")


def load_config():
    """
    Per-machine settings (serial port, WiFi URL, BLE device name). Falls
    back to empty values if config.json doesn't exist yet -- the relevant
    data-source function will raise a clear error if something's missing
    rather than failing silently.
    """
    if not os.path.exists(CONFIG_PATH):
        return {}
    try:
        # utf-8-sig so a file saved by Windows Notepad (which can add a
        # byte order mark) still loads instead of failing on an invisible
        # character
        with open(CONFIG_PATH, encoding="utf-8-sig") as f:
            config = json.load(f)
    except ValueError as exc:
        sys.exit(f"config.json is not valid JSON: {exc}\n"
                 "Compare it with config.example.json (no comments or trailing commas).")
    if not isinstance(config, dict):
        sys.exit("config.json must be a JSON object, like config.example.json")
    return config


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
    """Catch up the browser that just connected with the current state."""
    emit("connection_status", connection_state)


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
        reason = str(exc) or type(exc).__name__
        print(f"[ERROR] Data source failed: {reason}", flush=True)
        set_connection_status("error", reason)


# ---- turning whatever arrives into {pitch, roll, power} ----

MISSING = object()

# (key sent to the dashboard, flat key, nested group, key inside that group)
FIELDS = (
    ("pitch", "pitch", "imu", "pitch_deg"),
    ("roll", "roll", "imu", "roll_deg"),
    ("power", "power", "power", "power_w"),
)
# A message with none of these is not something the dashboard can use.
# (light is recognized so a light-only message is ignored quietly, since
# the dashboard doesn't display it yet.)
RECOGNIZED_KEYS = {"pitch", "roll", "power", "imu", "light"}


def clean_number(value):
    """
    A finite number, or None. Booleans, strings, NaN, and infinity all
    become None: NaN would be sent to the browser as an invalid JSON
    token, and a number that arrives as a string would silently break
    the rolling average (text concatenation instead of addition).
    """
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        return None
    try:
        number = float(value)
    except OverflowError:
        return None
    return number if math.isfinite(number) else None


def pick(reading, flat_key, group_key, nested_key):
    """Look for a field in its flat spot first, then inside its sensor group."""
    value = reading.get(flat_key, MISSING)
    if isinstance(value, dict):
        value = MISSING  # "power" is a whole sensor group here, not a number
    if value is MISSING:
        group = reading.get(group_key)
        if isinstance(group, dict):
            value = group.get(nested_key, MISSING)
    return value


def normalize_reading(reading):
    """
    The one seam between whatever the ESP32 actually sends and what the
    dashboard expects. dashboard.js only ever knows about a flat
    {pitch, roll, power} shape. It doesn't care whether the firmware sent
    that flat shape or grouped it by sensor the way Jennifer's draft does
    (imu.pitch_deg, imu.roll_deg, power.power_w, each with a health
    block). If the real schema changes, this is the only function to edit.

    A field the message does not mention at all is left out of the result,
    so the dashboard keeps showing its last value for a moment. A field
    that is present but null, or not a usable number, comes through as
    None, which the dashboard shows as "--" right away. That matches how
    Jennifer's serializer reports an invalid sensor: the measurement is
    null, not missing.
    """
    result = {}
    for out_key, flat_key, group_key, nested_key in FIELDS:
        value = pick(reading, flat_key, group_key, nested_key)
        if value is not MISSING:
            result[out_key] = clean_number(value)
    return result


_last_drop_warning = 0.0
_first_reading_logged = False


def warn_dropped(reason, message):
    """
    Bad messages get skipped instead of crashing the data source, but
    skipping silently makes firmware debugging miserable ("it says
    Connected but nothing shows up"). Print a short warning, at most once
    per second so a badly broken stream doesn't flood the terminal.
    """
    global _last_drop_warning
    now = time.monotonic()
    if now - _last_drop_warning < 1.0:
        return
    _last_drop_warning = now
    # ascii() so an odd character can't crash print() on a Windows console
    print(f"[WARN] Dropped a message ({reason}): {ascii(message)[:80]}", flush=True)


def emit_reading(reading, raw):
    global _first_reading_logged
    if not isinstance(reading, dict):
        warn_dropped("JSON is not an object", raw)
        return
    if not RECOGNIZED_KEYS & reading.keys():
        warn_dropped("no pitch, roll, power, or imu field", raw)
        return
    flat = normalize_reading(reading)
    if not flat:
        return  # recognized message, but nothing in it the dashboard displays
    if not _first_reading_logged:
        print("[INFO] First reading received", flush=True)
        _first_reading_logged = True
    socketio.emit("sensor_update", flat)


def process_reading(message):
    """
    Single entry point for a reading from any data source, regardless of
    transport. Accepts a dict (mock data, native Python already), raw
    bytes or a bytearray (serial and BLE hand over undecoded bytes; bleak
    specifically delivers a bytearray), or a string (websocket already
    decodes text frames on its own). A JSON array of readings is accepted
    too, in case the firmware ever batches. Anything malformed is dropped
    with a warning instead of being allowed to kill the data source
    thread. This is also where future logic goes, once instead of once
    per source:
      - validate schema
      - normalize units
      - reject stale/invalid sensor values
      - calculate any derived dashboard fields
    """
    raw = message
    try:
        if isinstance(message, dict):
            readings = [message]
        else:
            if isinstance(message, (bytes, bytearray)):
                if len(message) > MAX_MESSAGE_BYTES:
                    warn_dropped("message too large", raw[:80])
                    return
                try:
                    message = bytes(message).decode("utf-8")
                except UnicodeDecodeError:
                    warn_dropped("not valid UTF-8", raw)
                    return
            if not isinstance(message, str):
                warn_dropped("unsupported message type", raw)
                return
            if len(message) > MAX_MESSAGE_BYTES:
                warn_dropped("message too large", message[:80])
                return
            if not message.strip():
                return  # blank line or empty frame, nothing worth reporting
            try:
                parsed = json.loads(message)
            except (ValueError, RecursionError):
                # ValueError covers bad JSON and also numbers too long for
                # newer Pythons to convert; RecursionError covers absurdly
                # deep nesting
                warn_dropped("not valid JSON", raw)
                return
            readings = parsed[:MAX_BATCH_SIZE] if isinstance(parsed, list) else [parsed]

        for reading in readings:
            emit_reading(reading, raw)
    except Exception as exc:
        # Last resort: one bad message must never stop the stream.
        warn_dropped(f"unexpected error: {exc}", raw)


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
#
# Lines this accepts, so the firmware doesn't have to change how it prints:
#   {"pitch": ...}                       plain JSON, one object per line
#   sensor_json,{"imu": ...}             Jennifer's labeled line
#   I (1234) tag: {"pitch": ...}         JSON sent through ESP_LOGI
# Everything else on the port (boot messages, other log lines) is ignored.

SERIAL_TAG = b"sensor_json,"
LOG_PREFIX = re.compile(rb"^(?:\x1b\[[0-9;]*m)?[EWIDV] \(\d+\) [^:]*: ?")
COLOR_SUFFIX = re.compile(rb"(?:\x1b\[[0-9;]*m)+$")


def json_from_serial_line(line):
    """Return the JSON part of a serial line, or None if it isn't one of ours."""
    line = COLOR_SUFFIX.sub(b"", line.strip())
    line = LOG_PREFIX.sub(b"", line, count=1)
    if line.startswith(SERIAL_TAG):
        line = line[len(SERIAL_TAG):]
    if line.startswith(b"{") or (line.startswith(b"[") and line.endswith(b"]")):
        return line
    return None


def serial_data_loop(config):
    import serial

    port = config.get("serial_port")
    if not port:
        raise ValueError('config.json is missing "serial_port"')
    baud = int(config.get("serial_baud", 115200))

    while True:
        ser = None
        try:
            # serial_for_url takes a normal port name or a URL such as
            # rfc2217://localhost:4000, which is how Wokwi exposes a
            # simulated board's serial port.
            ser = serial.serial_for_url(port, baudrate=baud)
            print(f"Serial connected ({port})")
            set_connection_status("connected")

            while True:
                line = json_from_serial_line(ser.read_until(b"\n", MAX_SERIAL_LINE))
                if line:
                    process_reading(line)

        except (serial.SerialException, OSError) as exc:
            # A board that resets or gets reflashed drops its USB port for
            # a moment. Retrying means the dashboard survives that.
            reason = str(exc) or type(exc).__name__
            print(f"Serial connection lost: {reason}")
            print(f"Retrying in {RETRY_SECONDS} seconds...")
            set_connection_status("reconnecting", reason)
            time.sleep(RETRY_SECONDS)
        finally:
            if ser is not None:
                try:
                    ser.close()  # Windows won't let the port be reopened otherwise
                except Exception:
                    pass


# ---- WiFi / WebSocket (needs: pip install websocket-client) ----

def websocket_data_loop(config):
    import websocket

    url = config.get("wifi_url")
    if not url:
        raise ValueError('config.json is missing "wifi_url"')
    if not str(url).startswith(("ws://", "wss://")):
        raise ValueError('"wifi_url" must start with ws:// (for example ws://192.168.4.1/ws)')
    idle_timeout = float(config.get("wifi_idle_timeout", 10))

    while True:
        ws = None
        connected = False
        try:
            ws = websocket.WebSocket()
            ws.connect(url, timeout=5)
            # If the ESP32 reboots or loses power it never closes the
            # socket, so a plain recv() would block forever and the
            # dashboard would never reconnect. The firmware is expected to
            # keep pushing, so silence means the link died.
            ws.settimeout(idle_timeout)
            connected = True
            print("WiFi connected")
            set_connection_status("connected")

            while True:
                process_reading(ws.recv())

        except websocket.WebSocketTimeoutException:
            reason = (f"no data for {idle_timeout:g} seconds" if connected
                      else "connection attempt timed out")
        except (websocket.WebSocketException, OSError) as exc:
            reason = str(exc) or type(exc).__name__
        else:
            reason = "connection closed"
        finally:
            if ws is not None:
                try:
                    ws.close()
                except Exception:
                    pass

        print(f"WiFi connection lost: {reason}")
        print(f"Retrying in {RETRY_SECONDS} seconds...")
        set_connection_status("reconnecting", reason)
        time.sleep(RETRY_SECONDS)


# ---- Bluetooth LE (needs: pip install bleak) ----
#
# Discover by advertised name instead of relying on a hardcoded BLE
# address. macOS/CoreBluetooth does not expose a peripheral's real BLE
# MAC address to apps at all, so name-based discovery is what keeps this
# working the same way on macOS and Windows.

def ble_data_loop(config):
    import asyncio
    from bleak import BleakScanner, BleakClient
    try:
        from bleak.exc import BleakCharacteristicNotFoundError
    except ImportError:  # very old bleak
        BleakCharacteristicNotFoundError = ()

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
                    reason = "device disconnected"

            except BleakCharacteristicNotFoundError:
                reason = (f"connected, but characteristic {characteristic_uuid} was not found. "
                          "Check ble_characteristic against the firmware "
                          "(NimBLE writes UUID bytes in reverse order)")
            except Exception as exc:
                reason = str(exc) or type(exc).__name__

            print(f"BLE connection lost/failed: {reason}")
            set_connection_status("reconnecting", reason)
            print(f"Retrying in {RETRY_SECONDS} seconds...")
            await asyncio.sleep(RETRY_SECONDS)

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


def port_is_free(port):
    """
    True if nothing else is already listening on this port. Checked up
    front so a second copy of the dashboard fails with a plain message
    before it starts touching the serial port or BLE adapter.
    """
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        if sys.platform != "win32":
            # same setting the web server uses, so a port that is merely
            # in its post-restart wait period doesn't count as "in use"
            s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        try:
            s.bind(("0.0.0.0", port))
        except OSError:
            return False
    return True


def lan_address():
    """Best-effort address of this machine on the local network, for the startup message."""
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
            s.connect(("10.255.255.255", 1))  # UDP: sends nothing, just picks an interface
            return s.getsockname()[0]
    except OSError:
        return None


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--source", choices=DATA_SOURCES.keys(), default="mock",
        help="Where sensor data comes from (default: mock)",
    )
    parser.add_argument(
        "--port", type=int, default=5001,
        help="Port for the dashboard web page (default: 5001)",
    )
    parser.add_argument(
        "--debug", action="store_true",
        help="Flask debug mode. Leave off for demos: it exposes the debugger "
             "to everyone on the network, not just this machine.",
    )
    args = parser.parse_args()

    config = load_config()
    if not port_is_free(args.port):
        sys.exit(f"Port {args.port} is already in use. Is another copy of the dashboard "
                 f"still running? Close it, or start this one with --port {args.port + 1}.")
    print(f"Data source: {args.source}")
    print(f"Dashboard: http://localhost:{args.port}")
    ip = lan_address()
    if ip:
        print(f"From another device on the same network: http://{ip}:{args.port}")

    socketio.start_background_task(run_data_source, DATA_SOURCES[args.source], config)

    # use_reloader=False: the reloader spawns a second process that runs
    # this whole file again, including the data source above -- without
    # this, debug mode silently opens two serial/WiFi/BLE connections at
    # once. It's also just wrong for this app: auto-restart-on-save would
    # drop a live hardware connection every time a file is saved.
    socketio.run(app, host="0.0.0.0", port=args.port, debug=args.debug,
                 use_reloader=False, allow_unsafe_werkzeug=True)
