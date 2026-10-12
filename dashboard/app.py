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
go in config.json, not in this file. Copy config.example.json to start.
Each teammate keeps their own config.json, and git ignores it, because a
Mac's serial port name looks nothing like a Windows one.

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

MOCK_INTERVAL_SECONDS = 0.2   # 5 mock messages per second, close to the planned telemetry rate
RETRY_SECONDS = 2
MAX_MESSAGE_BYTES = 65536   # anything bigger is garbage, not a sensor reading
MAX_BATCH_SIZE = 500        # readings accepted from one message if it's a JSON array
MAX_SERIAL_LINE = 8192
CONFIG_PATH = os.path.join(os.path.dirname(__file__), "config.json")


def load_config():
    """
    Per-machine settings (serial port, WiFi URL, BLE device name). Returns
    an empty dict if config.json doesn't exist yet. Each data source checks
    for the settings it needs and raises a clear error if one is missing.
    """
    if not os.path.exists(CONFIG_PATH):
        return {}
    try:
        # utf-8-sig so a file saved by Windows Notepad, which can add an
        # invisible byte order mark at the start, still loads
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
    Save the connection state and send it to every open page. emit() only
    reaches pages that are already open, so a page opened later would
    never hear about the change and its badge would stay on "Connecting".
    Saving the state lets browser_connected() send it to each new page.
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
    Run a data source in the background thread. An exception in a thread
    only prints a traceback and ends the thread, so the page would sit on
    its last status with no sign of the problem. This catches it and shows
    the reason in the terminal and on the badge.
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
    # Optional. The angle the controller is steering toward (the sun target).
    # When these arrive, the page measures error from the target instead of
    # tilt from level. Firmware that never sends them gets the level view.
    ("pitch_target", "pitch_target", None, None),
    ("roll_target", "roll_target", None, None),
)
# A message with none of these is not something the dashboard can use.
# (light is recognized so a light-only message is ignored quietly, since
# the dashboard doesn't display it yet.)
RECOGNIZED_KEYS = {"pitch", "roll", "power", "imu", "light", "pitch_target", "roll_target"}


def clean_number(value):
    """
    A finite number, or None. Booleans, strings, NaN, and infinity all
    become None: NaN would be sent to the browser as an invalid JSON
    token, and a number that arrives as a string would silently break
    the rolling average.
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
    if value is MISSING and group_key:
        group = reading.get(group_key)
        if isinstance(group, dict):
            value = group.get(nested_key, MISSING)
    return value


def normalize_reading(reading):
    """
    Convert a message from the ESP32 into the flat shape dashboard.js
    uses: pitch, roll, power, and the optional targets. The firmware can
    send that flat shape or group fields by sensor the way Jennifer's
    draft does (imu.pitch_deg, imu.roll_deg, power.power_w). If the field
    names change, FIELDS above is the only thing to edit.

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
    Print why a message was skipped. Without this, a firmware mistake
    looks like "Connected, but nothing shows up". Limited to once per
    second so a broken stream doesn't flood the terminal.
    """
    global _last_drop_warning
    now = time.monotonic()
    if now - _last_drop_warning < 1.0:
        return
    _last_drop_warning = now
    # ascii() so an odd character can't crash print() on a Windows console
    print(f"[WARN] Dropped a message ({reason}): {ascii(message)[:80]}", flush=True)


def emit_reading(reading, raw):
    """Check one parsed message and send its values to the browsers."""
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
    Entry point for a message from any data source. Accepts a dict (mock
    data), bytes or a bytearray (serial and BLE; bleak hands over a
    bytearray), or a str (WebSocket text frames). A JSON array of readings
    also works, in case the firmware ever sends several at once. A bad
    message is dropped with a warning and never stops the data source.
    Checks that apply to every source belong here, not in each loop.
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
                # ValueError covers bad JSON and numbers too long for newer
                # Pythons to convert. RecursionError covers extremely deep
                # nesting.
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
    """
    Made-up readings for testing without hardware: a tilt that settles
    back to level, repeating every 40 seconds so the page keeps moving.
    """
    cycle = t % 40.0
    decay = math.exp(-0.08 * cycle)
    pitch = 8 * decay * math.sin(0.5 * cycle) + random.uniform(-0.3, 0.3)
    roll = 6 * decay * math.cos(0.4 * cycle) + random.uniform(-0.3, 0.3)
    power = 1.2 + 0.1 * math.sin(0.1 * t) + random.uniform(-0.05, 0.05)
    return {"pitch": round(pitch, 2), "roll": round(roll, 2), "power": round(power, 2)}


def generate_tracking_reading(t):
    """
    Like generate_mock_reading(), but following a slowly moving sun target
    instead of holding level, the way the real firmware does. Used with
    --mock-tracking.
    """
    reading = generate_mock_reading(t)
    pitch_target = 12 * math.sin(2 * math.pi * t / 120)
    roll_target = 8 * math.sin(2 * math.pi * t / 90)
    reading["pitch"] = round(reading["pitch"] + pitch_target, 2)
    reading["roll"] = round(reading["roll"] + roll_target, 2)
    reading["pitch_target"] = round(pitch_target, 2)
    reading["roll_target"] = round(roll_target, 2)
    return reading


def mock_data_loop(config):
    """Send a made-up reading 5 times a second, forever."""
    set_connection_status("connected")
    generate = generate_tracking_reading if config.get("mock_tracking") else generate_mock_reading
    t = 0
    while True:
        process_reading(generate(t))  # dict, passed straight through
        t += MOCK_INTERVAL_SECONDS
        time.sleep(MOCK_INTERVAL_SECONDS)


# ---- serial (needs: pip install pyserial) ----
#
# Lines this accepts, so the firmware doesn't have to change how it prints:
#   {"pitch": ...}                       plain JSON, one object per line
#   sensor_json,{"imu": ...}             Jen's labeled line
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


class SerialLines:
    """
    Splits a raw byte stream into lines. Reading whatever has arrived in
    one call (instead of pyserial's read_until, which reads one byte per
    call) keeps CPU use low even when the board prints a lot.
    """

    def __init__(self, max_line=MAX_SERIAL_LINE):
        self.buffer = bytearray()
        self.max_line = max_line

    def feed(self, chunk):
        self.buffer += chunk
        lines = []
        while True:
            end = self.buffer.find(b"\n")
            if end < 0:
                break
            lines.append(bytes(self.buffer[:end]))
            del self.buffer[:end + 1]
        if len(self.buffer) > self.max_line:
            self.buffer.clear()  # runaway line with no newline: drop it
        return lines


# USB vendor IDs used by ESP32 boards. The Heltec V4 the team uses is
# Espressif's own native USB (303A); the others are common USB-serial chips.
ESP_USB_VENDORS = {
    0x303A: "Espressif native USB",
    0x10C4: "Silicon Labs CP210x",
    0x1A86: "WCH CH340/CH343/CH9102",
    0x0403: "FTDI",
}


def describe_ports(ports):
    return ", ".join(f"{p.device} ({p.description})" for p in ports) or "none"


def pick_serial_port(ports):
    """
    Pick the board from serial.tools.list_ports.comports() output.
    Returns (port, how it was chosen), or (None, why not).
    """
    usb = [p for p in ports if p.vid is not None]
    known = [p for p in usb if p.vid in ESP_USB_VENDORS]
    if len(known) == 1:
        return known[0].device, ESP_USB_VENDORS[known[0].vid]
    espressif = [p for p in known if p.vid == 0x303A]
    if len(known) > 1 and len(espressif) == 1:
        return espressif[0].device, "Espressif native USB"
    if len(known) > 1:
        return None, ("found several possible boards: " + describe_ports(known) +
                      ". Unplug the extra one or pass --serial-port")
    if len(usb) == 1:
        return usb[0].device, "the only USB serial port"
    if not usb:
        return None, ("no ESP32 board found. Plug it in with a data cable "
                      "(some USB cables only charge), or pass --serial-port")
    return None, ("several USB serial ports and none look like an ESP32: " +
                  describe_ports(usb) + ". Pass --serial-port")


def port_group(port):
    """
    The group that owns a serial port on Linux: dialout on Ubuntu and Debian,
    uucp on Arch and Fedora. Ask system instead of guessing.
    """
    try:
        import grp
        return grp.getgrgid(os.stat(port).st_gid).gr_name
    except (ImportError, OSError, KeyError):
        return "dialout"


def explain_serial_error(exc, port):
    """Turn pyserial's error into something we can act on."""
    text = str(exc) or type(exc).__name__
    low = text.lower()
    if ("exclusively lock" in low or "resource busy" in low or "errno 16" in low
            or (os.name == "nt" and "access is denied" in low)):
        return (f"{port} is being used by another program (idf.py monitor, Arduino "
                "Serial Monitor, or another copy of the dashboard). Close it.")
    if "permission denied" in low and sys.platform.startswith("linux"):
        return (f"no permission to open {port}. Run: sudo usermod -aG {port_group(port)} $USER "
                "then log out and back in")
    if "no such file" in low or "cannot find the file" in low or "filenotfound" in low:
        return f"{port} is not there any more (unplugged, resetting, or renamed)"
    if "returned no data" in low or "device disconnected" in low or "clearcommerror" in low:
        return f"lost {port} (board unplugged or reset)"
    return text


def open_serial(serial, port, baud):
    """
    Open the port the way Espressif's own idf.py monitor does when told not
    to reset the board: DTR and RTS set before opening, then RTS released
    before DTR. On ESP32 boards those two lines drive reset and boot mode,
    so opening a port carelessly can reboot the board or hold it in the
    bootloader. Exclusive access means two programs can't both read the
    port and split the lines between them.
    """
    ser = serial.serial_for_url(port, baudrate=baud, timeout=0.5,
                                do_not_open=True, exclusive=True)
    try:
        ser.rts = True
        ser.dtr = True
    except Exception:
        pass  # network ports (socket://, rfc2217://) may not support these
    ser.open()
    try:
        ser.rts = False
        ser.dtr = False
    except Exception:
        pass
    return ser


QUIET_HINT_SECONDS = 8


def serial_data_loop(config):
    """Read lines from the board's USB serial port, reconnecting when it drops."""
    import serial
    from serial.tools import list_ports

    configured = config.get("serial_port") or "auto"
    baud = int(config.get("serial_baud", 115200))
    last_problem = None

    def report_problem(reason):
        # Printed once per new problem, so a long unplug doesn't fill the terminal.
        nonlocal last_problem
        if reason != last_problem:
            print(f"Serial: {reason}. Retrying every {RETRY_SECONDS} seconds...", flush=True)
            last_problem = reason
        set_connection_status("reconnecting", reason)

    while True:
        port = configured
        how = "from settings"
        if configured.lower() == "auto":
            port, how = pick_serial_port(list_ports.comports())
            if port is None:
                report_problem(how)
                time.sleep(RETRY_SECONDS)
                continue

        ser = None
        try:
            ser = open_serial(serial, port, baud)
            last_problem = None
            print(f"Serial connected: {port} ({how})", flush=True)
            set_connection_status("connected")

            splitter = SerialLines()
            opened_at = time.monotonic()
            any_bytes = False
            json_lines = 0
            last_other_line = None
            hinted = False

            while True:
                chunk = ser.read(ser.in_waiting or 1)
                if chunk:
                    any_bytes = True
                    for line in splitter.feed(chunk):
                        data = json_from_serial_line(line)
                        if data:
                            json_lines += 1
                            if hinted and json_lines == 1:
                                set_connection_status("connected")  # clear the hint
                            process_reading(data)
                        elif line.strip():
                            last_other_line = line.strip()

                # A connection that stays green with nothing on screen is the
                # most confusing failure, so say what is (or isn't) arriving.
                if (not hinted and json_lines == 0
                        and time.monotonic() - opened_at > QUIET_HINT_SECONDS):
                    hinted = True
                    if not any_bytes:
                        hint = ("port is open but the board is sending nothing. If idf.py "
                                "monitor shows output on this port, press the board's RST "
                                "button; it may be stuck in the bootloader")
                    else:
                        hint = ("the board is printing, but no line contains sensor JSON. "
                                f"Last line: {ascii(last_other_line)[:70]}. See "
                                "ESP32_INTEGRATION.md for the line format")
                    print(f"[HINT] Serial {hint}", flush=True)
                    set_connection_status("connected", hint)

        except (serial.SerialException, OSError) as exc:
            # A board that resets or gets reflashed drops its USB port for a
            # moment. Retrying (and re-detecting in auto mode, since the port
            # name can change) means the dashboard survives that.
            report_problem(explain_serial_error(exc, port))
            time.sleep(RETRY_SECONDS)
        finally:
            if ser is not None:
                try:
                    ser.close()  # Windows won't let the port be reopened otherwise
                except Exception:
                    pass


# ---- WiFi / WebSocket (needs: pip install websocket-client) ----

def websocket_data_loop(config):
    """Receive messages from the ESP32's WebSocket server, reconnecting when it drops."""
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
    """Subscribe to the ESP32's BLE notifications, rescanning when the link drops."""
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

                # The disconnect callback sets this event, which gives the
                # code below something to wait on. Sleeping in a loop
                # would never notice the link dropping.
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
            # Same setting the web server uses, so a port still in its
            # short wait period after a restart doesn't count as in use.
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
        "--serial-port", metavar="PORT",
        help="Serial port to use (overrides config.json). Default: find the board automatically",
    )
    parser.add_argument(
        "--mock-tracking", action="store_true",
        help="With --source mock: send a moving sun target too, like the real firmware",
    )
    parser.add_argument(
        "--list-ports", action="store_true",
        help="List the serial ports this computer can see, then exit",
    )
    parser.add_argument(
        "--debug", action="store_true",
        help="Flask debug mode. Leave off for demos: it exposes the debugger "
             "to everyone on the network, not just this machine.",
    )
    args = parser.parse_args()

    if args.list_ports:
        from serial.tools import list_ports
        ports = list_ports.comports()
        for p in ports:
            vid = f"{p.vid:04X}" if p.vid is not None else "----"
            print(f"{p.device:30s} vid {vid}  {p.description}")
        if not ports:
            print("No serial ports found.")
        picked, how = pick_serial_port(ports)
        print(f"\nAuto-detect would use: {picked} ({how})" if picked else f"\nAuto-detect: {how}")
        sys.exit(0)

    config = load_config()
    if args.serial_port:
        config["serial_port"] = args.serial_port
    if args.mock_tracking:
        config["mock_tracking"] = True
    if not port_is_free(args.port):
        sys.exit(f"Port {args.port} is already in use. Is another copy of the dashboard "
                 f"still running? Close it, or start this one with --port {args.port + 1}.")
    print(f"Data source: {args.source}")
    print(f"Dashboard: http://localhost:{args.port}")
    ip = lan_address()
    if ip:
        print(f"From another device on the same network: http://{ip}:{args.port}")

    socketio.start_background_task(run_data_source, DATA_SOURCES[args.source], config)

    # use_reloader=False: the reloader starts a second process that runs
    # this file again, data source included, so debug mode would open two
    # serial, WiFi, or BLE connections at once. Restarting on every file
    # save would also drop the hardware connection each time.
    socketio.run(app, host="0.0.0.0", port=args.port, debug=args.debug,
                 use_reloader=False, allow_unsafe_werkzeug=True)
