# ESP32 Integration Guide

What the dashboard expects from the ESP32 and what it does when something
goes wrong. The dashboard only listens, except for the BLE send rate,
which it can write.

## Quick start

**BLE** (Jennifer's firmware): flash it, make sure `config.json` has the
BLE fields from `config.example.json`, then

```
python app.py --source ble
```

The board advertises as `SolarYou-ASSTP`. The first scan on a Mac may ask
for Bluetooth permission.

**Serial**: close `idf.py monitor` (only one program can hold the port),
plug the board in with a data cable, then

```
python app.py --source serial
```

No config needed. The dashboard finds the board by its USB vendor ID. If
it picks the wrong port, `python app.py --list-ports` shows what it sees,
then pass `--serial-port <port>`. If a line shows up in `idf.py monitor`,
the dashboard can read it.

Open http://localhost:5001 in both cases.

## 1. Message format

Each message is one JSON object. These shapes all work:

```json
{"pitch": -2.35, "roll": 0.7, "power": 9.11}
```

```json
{"imu": {"pitch_deg": -2.35, "roll_deg": 0.7}, "power": {"power_w": 9.11}}
```

```json
[{"pitch": 1.0, "roll": 2.0}, {"pitch": 1.1, "roll": 2.1}]
```

The grouped shape matches `draft_sensor_message.json`. Extra fields
(`health`, `light`, `valid`, `sequence`) are ignored.

| Dashboard shows | Flat key | Grouped key | Unit |
|---|---|---|---|
| Pitch | `pitch` | `imu.pitch_deg` | degrees |
| Roll | `roll` | `imu.roll_deg` | degrees |
| Power | `power` | `power.power_w` | watts |
| Pitch target (optional) | `pitch_target` | | degrees |
| Roll target (optional) | `roll_target` | | degrees |

| In the message | On screen |
|---|---|
| a number | shown (angles to 1 decimal, power to 2) |
| `null` | `--` right away (sensor reported invalid) |
| missing | last value stays for 5 s, then `--` |
| a string, `true`/`false`, NaN, or infinity | treated as `null` |

A message doesn't need every field every time. One with none of the
fields above (and no `imu` or `light`) is dropped with a warning.

- Chart points use the arrival time, not a device timestamp.
- Round floats before adding them to the JSON. cJSON prints a `float` with
  15 to 17 digits, which roughly doubles the flat message. That matters
  over BLE.
- To show different field names, edit the `FIELDS` table in
  `normalize_reading()` in `app.py` and rerun the tests.

## 2. Transports

| | Serial | WiFi | BLE |
|---|---|---|---|
| ESP32 role | prints lines | WebSocket server | GATT server |
| Dashboard role | reads the port | WebSocket client | BLE central |
| Config | `serial_port` (optional) | `wifi_url` | `ble_name`, `ble_characteristic` |
| Long messages | fine | fine | split across notifications |
| Reconnects on its own | yes | yes | yes |

### Serial

Any of these lines works. Everything else on the port (boot output, log
lines, `sample:` lines) is ignored.

```
{"pitch":1.0,"roll":2.0}
sensor_json,{"imu":{...},"power":{...}}
I (1234) telemetry: {"pitch":1.0,"roll":2.0}
```

- End each message with a newline and print the whole line in one call,
  so another task's output can't split it.
- `printf("%f")` of a NaN prints `nan`, which isn't valid JSON, and the
  line is dropped. Send `null` instead.
- The dashboard locks the port the same way `idf.py monitor` does, so if
  one has it, the other says so instead of both reading half the lines.
- Opening the port doesn't reset the board. The dashboard sets DTR and
  RTS in the same order `idf.py monitor --no-reset` uses.
- The Heltec V4 uses native USB, which ignores the baud rate. A board with
  a USB-serial chip has to match the firmware (115200 by default).
- After a reset or reflash the port may come back under a new name. The
  dashboard retries every 2 s and finds it again in auto mode.
- If the port is open but nothing usable arrives for 8 s, the terminal
  says whether nothing is coming in (press RST, the board may be in the
  bootloader) or lines arrive without sensor JSON (the last one is shown).

At 115200 baud a USB-serial chip carries about 11,500 bytes a second. The
flat line (about 42 bytes) at 10 a second uses about 4%. Jennifer's full
`sensor_json,` line (860 to 950 bytes) at 10 a second uses about 80%, so
keep that one to once or twice a second. Print from the main loop, not
from inside the 50 Hz control step.

Jennifer's standalone sensor app (`SolarYou_Sensor`) also works: turn on
`SY_CONSOLE_PRINT_JSON`.

### WiFi (WebSocket)

- The ESP32 runs the server and the dashboard connects to `wifi_url`
  (`ws://<esp32-ip>/ws`).
- Firmware needs `esp_http_server` with `CONFIG_HTTPD_WS_SUPPORT` and a
  `/ws` handler with `is_websocket = true`.
- **The ESP32 has to push.** The dashboard never sends a frame, so the
  `ws_echo_server` pattern won't work. Save the socket with
  `httpd_req_to_sockfd(req)` when the client connects, then send text
  frames with `httpd_ws_send_frame_async()` through `httpd_queue_work()`,
  one JSON object per frame. The `wss_server` example does this.
- If nothing arrives for 10 s the dashboard reconnects
  (`wifi_idle_timeout`). That's how it notices a rebooted board.
- In softAP mode the board is usually `192.168.4.1`. On a router, print
  the DHCP address at boot. Use the IP rather than `esp32.local`.

References:

- https://github.com/espressif/esp-idf/tree/master/examples/protocols/http_server/ws_echo_server
- https://github.com/espressif/esp-idf/tree/master/examples/protocols/https_server/wss_server
- https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/protocols/esp_http_server.html

### BLE

Jennifer's firmware already does what's below. The full byte-level
format is in `BLE_PROTOCOL.md`.

The ESP32 is the peripheral. The dashboard scans for `ble_name`,
connects, and subscribes to the telemetry characteristic.

| | UUID | NimBLE bytes (reversed) |
|---|---|---|
| Service | `47e8841f-04c9-4ae2-af68-a7b6f4eb293d` | `0x3d, 0x29, 0xeb, 0xf4, 0xb6, 0xa7, 0x68, 0xaf, 0xe2, 0x4a, 0xc9, 0x04, 0x1f, 0x84, 0xe8, 0x47` |
| Telemetry (notify) | `f2d2680c-66bc-4e24-8eac-d8013a99320b` | `0x0b, 0x32, 0x99, 0x3a, 0x01, 0xd8, 0xac, 0x8e, 0x24, 0x4e, 0xbc, 0x66, 0x0c, 0x68, 0xd2, 0xf2` |
| Send rate (read/write) | `f2d2680c-66bc-4e24-8eac-d8013a99320c` | `0x0c, 0x32, 0x99, 0x3a, 0x01, 0xd8, 0xac, 0x8e, 0x24, 0x4e, 0xbc, 0x66, 0x0c, 0x68, 0xd2, 0xf2` |

- The name has to be in the advertisement or scan response, not only the
  GAP name.
- NimBLE stores UUID bytes in reverse order. If the dashboard connects
  and then says the characteristic wasn't found, check this first.
- A notification holds MTU - 3 bytes. The firmware asks for an MTU of
  185; if the phone or laptop only allows 23, longer messages are split
  with a 6-byte header and joined back together on the dashboard side.
- Send rate: writing `{"set_rate_hz":5}` asks for 5 a second, and reading
  returns `{"rate_hz":5}`. Allowed rates are 1, 2, 5, and 10. It only
  changes how often readings are sent, not the 50 Hz control loop.
- Don't put `rate_hz` in regular telemetry. The dashboard treats any
  notification with it as a rate reply.
- Firmware without the rate characteristic still works; the selector says
  so.
- If two boards share a name, the dashboard connects to the first one it
  finds. Give each its own name and match `ble_name`.

Reference: https://github.com/espressif/esp-idf/tree/master/examples/bluetooth/nimble/bleprph

## 3. Settings

`config.json` (copy `config.example.json`, don't commit it):

| Key | Used by | Default | Notes |
|---|---|---|---|
| `serial_port` | serial | `auto` | `auto`, a port name, or a URL like `rfc2217://localhost:4000` |
| `serial_baud` | serial | 115200 | ignored by native USB |
| `wifi_url` | wifi | | must start with `ws://` |
| `wifi_idle_timeout` | wifi | 10 | seconds of silence before reconnecting |
| `ble_name` | ble | | advertised device name |
| `ble_characteristic` | ble | | telemetry UUID |
| `ble_rate_characteristic` | ble | `...320c` | only if the firmware uses a different one |

| Flag | Meaning |
|---|---|
| `--source` | `mock` (default), `serial`, `wifi`, `ble` |
| `--port` | web page port, default 5001 |
| `--serial-port` | serial port, overrides `config.json` |
| `--mock-tracking` | mock data with a moving sun target |
| `--log` | save readings in mock mode too |
| `--no-log` | don't save readings |
| `--list-ports` | list serial ports and exit |
| `--debug` | Flask debug mode, exposes the debugger to the network |

## 4. Troubleshooting

| Badge | Meaning |
|---|---|
| Connecting... | starting up |
| Connected, waiting for data | link is up, nothing has arrived yet |
| Connected (green) | data is arriving |
| Connected, no recent data | nothing for 5 s |
| Reconnecting... | link dropped, retrying. Hover for the reason. |
| Connection Error (red) | settings problem. Hover for the reason. |

| You see | Likely cause |
|---|---|
| waiting for data, terminal quiet | serial: lines don't match section 2. WiFi: server isn't pushing. BLE: notify not enabled. |
| `Dropped a message (not valid JSON)` | truncated message or a `nan` from printf. Over BLE, check the MTU. |
| `being used by another program` | `idf.py monitor`, Arduino Serial Monitor, or another dashboard has the port |
| `no ESP32 board found` | not plugged in, charge-only cable, or a missing driver on Windows |
| `found several possible boards` | unplug one or pass `--serial-port` |
| `no permission to open` (Linux) | run the `usermod` command it prints, then log out and back in |
| `board is sending nothing` | press RST, the board may be in the bootloader |
| `no line contains sensor JSON` | the board prints, but not in a format from section 2 |
| `Dropped a message (no pitch, roll, power, or imu field)` | different field names |
| `no data for 10 seconds` | ESP32 stopped pushing or rebooted |
| `characteristic ... was not found` | UUID byte order (see BLE above) |
| `Could not find BLE device` | not advertising, different name, or Bluetooth off |
| `Bluetooth device is turned off` | turn Bluetooth on. Can also show once at startup on a Mac. |
| `Handshake status 404` | wrong path in `wifi_url` |
| `Port 5001 is already in use` | another copy is running, or use `--port 5002` |

Warnings print at most once a second. `[INFO] First reading received`
confirms data got through. A `ConnectionError` traceback when a browser
tab closes is harmless.

## 5. Recovery

| Situation | What happens |
|---|---|
| Cable unplugged, board reset or reflashed | retries every 2 s, finds the port again |
| Another program has the serial port | says so once, connects when it's released |
| Board loses power or WiFi drops | reconnects after 10 s of silence |
| BLE link drops | rescans and reconnects |
| One bad message | dropped, the stream continues |
| Wrong settings | red badge with the reason, no endless retrying |

## 6. Testing without a board

Unit tests, no hardware or network:

```
python -m unittest discover -s tests -v
```

Fake ESP32 over WiFi:

```
pip install websockets
python tools/fake_esp32_ws.py                  # flat messages
python tools/fake_esp32_ws.py --schema nested  # grouped messages
```

Set `"wifi_url": "ws://127.0.0.1:8765/ws"` and run
`python app.py --source wifi`.

Wokwi can forward a simulated board's serial port: add
`rfc2217ServerPort = 4000` under `[wokwi]` in `wokwi.toml`, keep the
simulator tab visible, set `"serial_port": "rfc2217://localhost:4000"`,
and run `python app.py --source serial`.

