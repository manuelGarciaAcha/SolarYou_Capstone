# ESP32 Integration Guide

What the dashboard expects from the ESP32 side, and what it does when
something goes wrong. The dashboard only receives. It never sends
anything to the ESP32.

Pick a data source with `python app.py --source serial|wifi|ble`.
Mock data (the default) needs nothing from the firmware.

**Shortest path:** serial. Jennifer's sensor component already prints its
JSON with a `sensor_json,` label in front, and the dashboard reads that
as is. Turn JSON printing on, plug the board in, set `serial_port`.

## 1. What the dashboard accepts

Each message is one JSON object. These shapes all work:

Flat:

```json
{"pitch": -2.35, "roll": 0.7, "power": 9.11}
```

Grouped by sensor, as in `draft_sensor_message.json` (extra fields such
as `health`, `light`, `valid`, `sequence` are ignored for now):

```json
{"imu": {"pitch_deg": -2.35, "roll_deg": 0.7}, "power": {"power_w": 9.11}}
```

An array of either, if the firmware ever sends several readings at once:

```json
[{"pitch": 1.0, "roll": 2.0}, {"pitch": 1.1, "roll": 2.1}]
```

| Dashboard shows | Flat key | Grouped key | Unit |
|---|---|---|---|
| Pitch | `pitch` | `imu.pitch_deg` | degrees |
| Roll | `roll` | `imu.roll_deg` | degrees |
| Power | `power` | `power.power_w` | watts |

What the dashboard does with each value:

| In the message | On screen |
|---|---|
| a number | shown to 2 decimals |
| `null` | `--` immediately (the sensor says it is invalid) |
| not mentioned at all | the last value stays for 5 seconds, then `--` |
| a string, `true`/`false`, NaN, or infinity | treated like `null` |

So a message does not have to carry every field every time. A message
with none of pitch, roll, power, imu, or light in it is dropped with a
warning.

Notes:

- Jennifer's serializer already fits this. It prints compact single-line
  JSON, and it writes `null` (not a number) for a measurement whose sensor
  is invalid, which is exactly what the dashboard expects.
- Points on the chart are stamped with the time the message arrives, not
  a device timestamp.
- The chart holds the last 30 points, so 5 messages per second is a 6
  second window and 1 per second is 30 seconds. The server kept up with
  about 100 messages per second in testing. The browser side was only
  tested at much lower rates.
- Floats from firmware print long. cJSON writes a `float` converted to
  `double` with 15 to 17 digits (`-2.3499999046325684`). That makes the
  flat message about 80 bytes instead of 40 (and Jennifer's current full message
  about 940 bytes instead of 850). Round before adding a number to the
  JSON (for example to 2 decimals) if size matters. It does over BLE.

## 2. Transports

| | Serial | WiFi | BLE |
|---|---|---|---|
| ESP32 role | prints lines | WebSocket server | GATT server |
| Dashboard role | reads the port | WebSocket client | BLE central |
| Config | `serial_port` | `wifi_url` | `ble_name`, `ble_characteristic` |
| Size limit | none that matters | none that matters | roughly 20 bytes by default, 512 at the very most |
| Full message fits | yes | yes | **no (about 940 bytes)** |
| Recovers by itself | yes | yes | yes |

### Serial

Any of these lines works. Everything else on the port (boot output, log
lines, the human-readable `sample:` and `imu_values:` lines) is
ignored.

```
{"pitch":1.0,"roll":2.0}
sensor_json,{"imu":{...},"power":{...}}
I (1234) telemetry: {"pitch":1.0,"roll":2.0}
```

- End each message with a newline (`\n` or `\r\n`).
- Only one program can hold the port. Close `idf.py monitor` before
  starting the dashboard, and the other way around.
- Opening the port can reboot some boards. That is expected.
- Find the port: Mac `ls /dev/tty.usb*`, Windows Device Manager (COMx).
  The name can change if the board is plugged into a different USB port.
- If the board resets or is reflashed, its port disappears for a moment.
  The dashboard keeps retrying every 2 seconds and carries on when it is
  back.
- `serial_port` can also be a URL. `rfc2217://localhost:4000` is how
  Wokwi exposes a simulated board (see section 6).

### WiFi (WebSocket)

- The ESP32 runs the server. The dashboard connects to
  `ws://<esp32-ip>/ws` (`wifi_url`, must start with `ws://`).
- Firmware needs `esp_http_server` with `CONFIG_HTTPD_WS_SUPPORT`
  enabled and a URI handler for `/ws` with `is_websocket = true`.
- **The ESP32 has to push.** The dashboard never sends a frame, so the
  server cannot wait for a request. The ws_echo_server example only sends
  its async message after the client sends one. What is needed:
  1. When the client connects, the handler is called once with
     `HTTP_GET`. Save the socket with `httpd_req_to_sockfd(req)`.
  2. On a timer or from a task, send with `httpd_ws_send_frame_async()`
     through `httpd_queue_work()`, as a text frame
     (`HTTPD_WS_TYPE_TEXT`), one whole JSON object per frame.
  The `wss_server` example shows the periodic version: it loops over the
  connected clients and pushes without any request.
- **Keep it pushing.** If the dashboard hears nothing for 10 seconds it
  assumes the link died and reconnects (`wifi_idle_timeout` changes
  this). A rebooted or powered-off ESP32 never closes its socket, so
  this is how the dashboard notices.
- IP address: if the ESP32 hosts its own network (softAP) the default is
  `192.168.4.1` and the laptop has to join that network. If it joins a
  router it gets a DHCP address, so print it at boot and copy it into the
  config. An IP is safer than a name like `esp32.local`, which depends on
  the computer supporting mDNS.

References:

- https://github.com/espressif/esp-idf/blob/master/examples/protocols/http_server/ws_echo_server/README.md
- https://github.com/espressif/esp-idf/blob/master/examples/protocols/https_server/wss_server/main/wss_server_example.c
- https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/protocols/esp_http_server.html

### BLE

The ESP32 is the peripheral. The dashboard scans for it by name,
connects, and subscribes to notifications.

Firmware needs:

1. A device name matching `ble_name` (`SolarYou-ASSTP`), in the
   advertisement data or scan response, not only set as the GAP name.
2. One GATT service with one characteristic that has the **NOTIFY**
   property, using these UUIDs:

   | | String | NimBLE bytes |
   |---|---|---|
   | Service | `47e8841f-04c9-4ae2-af68-a7b6f4eb293d` | `0x3d, 0x29, 0xeb, 0xf4, 0xb6, 0xa7, 0x68, 0xaf, 0xe2, 0x4a, 0xc9, 0x04, 0x1f, 0x84, 0xe8, 0x47` |
   | Characteristic | `f2d2680c-66bc-4e24-8eac-d8013a99320b` | `0x0b, 0x32, 0x99, 0x3a, 0x01, 0xd8, 0xac, 0x8e, 0x24, 0x4e, 0xbc, 0x66, 0x0c, 0x68, 0xd2, 0xf2` |

   NimBLE wants the bytes in reverse order compared to the written
   string, which is easy to get wrong. The bytes above are already
   reversed. If the dashboard connects and then reports that the
   characteristic was not found, check this first.
3. A notification for each update, payload is UTF-8 JSON text.

Message size:

- A notification carries at most (MTU - 3) bytes. The default MTU is 23,
  so 20 bytes, too small for even the flat message. Set a larger
  preferred MTU on the ESP32 (look for `ble_att_set_preferred_mtu` or
  `CONFIG_BT_NIMBLE_ATT_PREFERRED_MTU`) so the dashboard side can
  negotiate up.
- Even at the maximum, a notification tops out around 512 bytes, and a
  Mac will usually negotiate less. Jennifer's code example full message is about 940
  bytes, so over BLE send the flat message (rounded, about 40 bytes). The
  dashboard accepts it as is.

Other notes:

- If two boards advertise the same name the dashboard connects to
  whichever it finds first. Give each board its own name for now (for
  example `SolarYou-ASSTP-01`) and match `ble_name` to it.
- On a Mac, the first BLE scan may trigger a Bluetooth permission prompt
  for the app running Python. If the scan hangs or fails, check System
  Settings, Privacy & Security, Bluetooth.
- If the link drops, the dashboard rescans and reconnects on its own.

Reference: https://github.com/espressif/esp-idf/blob/master/examples/bluetooth/nimble/bleprph/README.md

## 3. Settings

`config.json` (copy `config.example.json`, never commit it):

| Key | Used by | Default | Notes |
|---|---|---|---|
| `serial_port` | serial | none | port name or URL |
| `serial_baud` | serial | 115200 | ignored by `socket://` |
| `wifi_url` | wifi | none | must start with `ws://` |
| `wifi_idle_timeout` | wifi | 10 | seconds of silence before reconnecting |
| `ble_name` | ble | none | advertised device name |
| `ble_characteristic` | ble | none | UUID string, as written above |

Command line:

| Flag | Meaning |
|---|---|
| `--source` | `mock` (default), `serial`, `wifi`, `ble` |
| `--port` | web page port, default 5001 |
| `--debug` | Flask debug mode. Off by default because it exposes the debugger page to everyone on the network. |

## 4. What the badge and terminal tell you

The badge in the dashboard header:

| Badge | Meaning |
|---|---|
| Connecting... (yellow) | starting up |
| Connected, waiting for data (yellow) | link is up, nothing has arrived yet |
| Connected (green) | data is arriving |
| Connected, no recent data (yellow) | link is up but nothing for 5 seconds |
| Reconnecting... (yellow) | link dropped, retrying. Hover for the reason. |
| Connection Error (red) | a settings problem. Hover for the reason. |

If the dashboard page itself loses its server (laptop asleep, server
restarted), the badge goes to Reconnecting and the page recovers on its
own.

| You see | Likely cause |
|---|---|
| waiting for data, nothing in terminal | serial: lines do not match section 2. WiFi: server is not pushing (echo example). BLE: notify not enabled. |
| `[WARN] Dropped a message (not valid JSON)` | truncated or malformed message. Over BLE, check the MTU. |
| `[WARN] Dropped a message (no pitch, roll, power, or imu field)` | JSON arrived but with different field names |
| `[WARN] Dropped a message (message too large)` | over 64 KB, probably wrong baud rate or garbage |
| `Reconnecting... no data for 10 seconds` | ESP32 stopped pushing or rebooted |
| `characteristic ... was not found` | UUID mismatch, check the byte order |
| `Handshake status 404` | wrong path in `wifi_url` |
| `Could not find BLE device` | not advertising, name differs, or Bluetooth is off |
| `Port 5001 is already in use` | another copy is running. Close it or use `--port 5002`. |

Warnings print at most once per second so a bad stream cannot flood the
terminal. `[INFO] First reading received` confirms the first good
message got through.

A `ConnectionError` traceback in the terminal when a browser tab closes
is dev server noise. Ignore it.

## 5. Recovery

| Situation | What happens |
|---|---|
| Serial cable unplugged, board reset or reflashed | retries every 2 seconds |
| ESP32 loses power, reboots, WiFi drops | no data for 10 seconds, then reconnects every 2 seconds |
| ESP32 closes the WebSocket cleanly | reconnects after 2 seconds |
| BLE link drops | rescans and reconnects |
| One malformed message | dropped, the stream continues |
| Wrong settings (missing key, bad URL scheme) | red badge with the reason, no endless retrying |

## 6. Trying it without a board

No hardware, WiFi path:

```
pip install websockets
python tools/fake_esp32_ws.py                  # flat messages
python tools/fake_esp32_ws.py --schema nested  # Jennifer's example (grouped shape)
```

then set `"wifi_url": "ws://127.0.0.1:8765/ws"` and run
`python app.py --source wifi`. It is a good reference for what a correct
stream looks like.

Simulated firmware, through serial. Wokwi can expose the
simulated board's serial port (documented by Wokwi, not yet tried by us):

1. In `wokwi.toml`, under `[wokwi]`, add `rfc2217ServerPort = 4000`.
2. Switch JSON printing on in the validation app (`SY_CONSOLE_PRINT_JSON`).
3. Start the simulator and keep its tab visible. Wokwi pauses when the
   tab is hidden.
4. Set `"serial_port": "rfc2217://localhost:4000"` and run
   `python app.py --source serial`.

Message logic checks (no hardware, no network):

```
python -m unittest discover -s tests -v
```

Run these after editing `normalize_reading()` for the final field names.

## 7. Decisions to make together

1. Which transport first. Serial needs no networking code and gets real
   data on screen fastest.
2. Flat or grouped messages per transport. BLE has to be flat (or split
   into several small messages).
3. Update rate. The chart is happiest at roughly 1 to 5 messages per
   second.
4. Whether the dashboard should show the light sensor and
   voltage/current. Right now only pitch, roll, and power are displayed.
5. Whether messages should carry a device timestamp.

## 8. Tested, and not tested

Tested against stand-ins: a simulated serial port (including an unplug),
a simulated WebSocket server that went silent, closed, refused, sent
binary frames, batches, and oversize messages, a simulated BLE library,
random and malformed input, the real Socket.IO browser client, and both
the newest and the oldest allowed library versions.

Not tested: a real ESP32, a real BLE radio, Wokwi's serial forwarding
itself, Windows, or how the page looks in a real browser. Expect the
first real connection to turn something up.

## 9. Not built yet

- Saving raw readings to disk (history, TinyML training data)
- Showing the health and fault information
- Displaying light, voltage, and current values
