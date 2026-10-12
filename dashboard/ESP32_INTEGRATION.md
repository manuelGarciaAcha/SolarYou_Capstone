# ESP32 Integration Guide

What the dashboard expects from the ESP32 side, and what it does when
something goes wrong. The dashboard only receives. It never sends
anything to the ESP32.

Pick a data source with `python app.py --source serial|wifi|ble`.
Mock data (the default) needs nothing from the firmware.

## Serial quickstart (Demo 1)

Serial is the shortest path to real data on screen. It needs no WiFi or
Bluetooth code on the ESP32, only printing.

**On the ESP32:** Jennifer's `jennifer-uart` branch already does this.
`main/uart_telemetry.c` prints one JSON line every 200 ms (5 a second)
from the telemetry slot in `app_main`. Pick the mode with
`idf.py menuconfig` -> SolarYou application -> Runtime mode:

- **Sensor/dashboard test** (default): reads the real IMU, never touches
  the servos. Use this first.
- **Full integration**: runs the PID and moves the servos.

Lines look like `{"pitch":1.23,"roll":-0.50,"power":null}`, which the
dashboard reads as they are.

Suggested upgrade (drop-in replacement for `main/uart_telemetry.c`, same
function and header). It sends `null` instead of `nan` for a bad value,
sends power when it's there, and adds the light target in full
integration mode:

```c
#include "uart_telemetry.h"

#include <math.h>
#include <stdio.h>
#include "sdkconfig.h"

// Writes one value for the JSON line. A value that isn't a real number
// (NaN or infinity) is sent as null, so the dashboard shows "--" for that
// field. Printing NaN with %f would give "nan", which isn't valid JSON, and
// the dashboard would have to drop the whole line.
static void format_value(char *out, size_t size, float value)
{
    if (isfinite(value)) {
        snprintf(out, size, "%.2f", (double)value);
    } else {
        snprintf(out, size, "null");
    }
}

void uart_telemetry_send(const system_state_t *state)
{
    if (state == NULL) {
        return;
    }

    char pitch[16];
    char roll[16];
    char power[16];
    format_value(pitch, sizeof(pitch), state->pitch_imu);
    format_value(roll, sizeof(roll), state->roll_imu);
    format_value(power, sizeof(power), state->power);

#if defined(CONFIG_SOLARYOU_RUNTIME_FULL_CONTROL) && CONFIG_SOLARYOU_RUNTIME_FULL_CONTROL
    // In full control the PID steers toward the light target, so send it.
    // The dashboard then shows error from the target instead of tilt from level.
    char pitch_target[16];
    char roll_target[16];
    format_value(pitch_target, sizeof(pitch_target), state->pitch_light);
    format_value(roll_target, sizeof(roll_target), state->roll_light);
    printf("{\"pitch\":%s,\"roll\":%s,\"power\":%s,\"pitch_target\":%s,\"roll_target\":%s}\n",
           pitch, roll, power, pitch_target, roll_target);
#else
    // Motors are off in sensor/dashboard test mode, so there is no target.
    printf("{\"pitch\":%s,\"roll\":%s,\"power\":%s}\n", pitch, roll, power);
#endif
    fflush(stdout);
}
```

It goes with three small edits in `main/ESP_SolarYou.c`:

```c
#include <math.h>   // with the other includes, for NAN

// 1. Wherever an IMU update fails (the #else branch with
//    "IMU telemetry update failed", and the "Stabilization task failed"
//    branch), mark the angles as unknown so the dashboard shows "--"
//    instead of the last good value:
curr_state.pitch_imu = NAN;
curr_state.roll_imu = NAN;

// 2. Right after sys_state_reset(&curr_state), so power starts as
//    unknown instead of 0:
curr_state.power = NAN;

// 3. In the telemetry slot, before uart_telemetry_send(&curr_state):
sensor_power_sample_t power = {0};
if (sensor_adapter_read_power(&power)) {
    curr_state.voltage = power.voltage_v;
    curr_state.current = power.current_a;
    curr_state.power = power.power_w;
} else {
    curr_state.power = NAN;
}
```

Checked with gcc under the flags `main/CMakeLists.txt` uses
(`-Wall -Wextra -Wconversion -Wdouble-promotion`, plus `-Werror`), in
both runtime modes, and the dashboard parsed every line it printed.
`sensor_adapter_read_power()` returns right away without touching I2C
when the INA219 isn't there, so calling it every 200 ms costs nothing in
test mode. Lines are at most about 85 bytes, under 4% of 115200 baud at
5 a second.

The power read and the IMU read run one after the other in the same
loop, so the two I2C sensors are never read at the same time and no bus
mutex is needed. If either read moves to its own FreeRTOS task, the
mutex question comes back.

Jennifer's standalone sensor app (`SolarYou_Sensor`) also still works:
turn on `SY_CONSOLE_PRINT_JSON` and it prints `sensor_json,{...}` lines
the dashboard reads as they are.

**On the laptop:**

1. Close `idf.py monitor`. Only one program can have the port open, and
   the dashboard says so if another one does.
2. Plug the board in with a data cable.
3. `python app.py --source serial`
4. Open `http://localhost:5001`

No `config.json` is needed. The dashboard finds the board by its USB
vendor ID: Espressif's own (the Heltec V4 uses the ESP32-S3's native USB)
or one of the common USB-serial chips. If it picks the wrong port or
can't decide, `python app.py --list-ports` shows what it sees, then pass
`--serial-port <port>`.

Rule of thumb: if a line shows up in `idf.py monitor`, the dashboard can
read it.

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
| Pitch target (optional) | `pitch_target` | none | degrees |
| Roll target (optional) | `roll_target` | none | degrees |

What the dashboard does with each value:

| In the message | On screen |
|---|---|
| a number | shown (angles to 1 decimal, power to 2) |
| `null` | `--` immediately (the sensor says it is invalid) |
| not mentioned at all | the last value stays for 5 seconds, then `--` |
| a string, `true`/`false`, NaN, or infinity | treated like `null` |

So a message does not have to carry every field every time. A message
with none of these fields (or `imu` or `light`) in it is dropped with a
warning.

Notes:

- Jennifer's serializer already fits this. It prints compact single-line
  JSON and writes `null` (not a number) for a measurement whose sensor is
  invalid.
- Points on the chart are stamped with the time the message arrives, not
  a device timestamp.
- The pitch and roll chart shows the last 30 seconds and the power chart
  the last 60, whatever the message rate. In testing the server kept up
  with about 100 messages a second and the page with 200.
- Floats from firmware print long. cJSON writes a `float` converted to
  `double` with 15 to 17 digits (`-2.3499999046325684`). That makes the
  flat message about 80 bytes instead of 40 and Jennifer's full message
  about 940 bytes instead of 850. Round before adding a number to the
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
lines, error prints, Jennifer's human-readable `sample:` and `imu_values:`
lines) is ignored.

```
{"pitch":1.0,"roll":2.0}
sensor_json,{"imu":{...},"power":{...}}
I (1234) telemetry: {"pitch":1.0,"roll":2.0}
```

- End each message with a newline (`\n` or `\r\n`), and print the whole
  line in one call so another task's output can't split it.
- `printf` of a NaN prints `nan`, which is not valid JSON, so that whole
  line is dropped (with a warning). The helper above sends `null` instead.
- Only one program can hold the port. The dashboard opens it with the
  same exclusive lock `idf.py monitor` uses, so if one has it, the other
  gets a clear "being used by another program" message instead of both
  reading half the lines.
- Opening the port should not reset the board. The dashboard sets the
  DTR and RTS lines (which drive reset and boot mode on ESP32 boards) in
  the same order `idf.py monitor` uses when told not to reset.
- Port names: Mac `/dev/cu.usbmodem...` (native USB) or
  `/dev/cu.usbserial-...` (USB-serial chip), Linux `/dev/ttyACM0` or
  `/dev/ttyUSB0`, Windows `COM3` and so on. With auto-detection you
  normally don't need to know it.
- Native USB (the Heltec V4) ignores the baud rate. On a board with a
  USB-serial chip it has to match the firmware (115200 is the ESP-IDF
  default).
- When the board resets or is reflashed, its port disappears for a
  moment and can come back under a different name. The dashboard retries
  every 2 seconds and, in auto mode, finds it again.
- If the port is open but nothing usable arrives for 8 seconds, the
  terminal and the badge tooltip say which: nothing at all is coming in
  (press RST, the board may be sitting in the bootloader), or lines are
  coming in but none contain sensor JSON (the last line is shown).
- `serial_port` can also be a URL. `rfc2217://localhost:4000` is how
  Wokwi exposes a simulated board (see section 6).

How much a serial link can carry. A USB-serial chip at 115200 baud moves
about 11,500 bytes a second. Native USB is much faster.

| Line | Size | At 10 per second | Share of a 115200 link |
|---|---|---|---|
| flat `{"pitch":..,"roll":..,"power":..}` | about 42 bytes | 0.4 KB/s | about 4% |
| Jennifer's full `sensor_json,` line | 860 to 950 bytes | 8.6 to 9.5 KB/s | 75 to 83% |

So the flat line is fine at 5 to 10 per second on any board. The full
message at that rate would nearly fill a 115200 link, which can slow the
task doing the printing. On a USB-serial board keep the full message to
once or twice a second. Either way, print from the loop, not from inside
a fast (50 Hz) control step.

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
  Mac will usually negotiate less. Jennifer's full message is about 940
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
| `serial_port` | serial | `auto` | `auto`, a port name, or a URL |
| `serial_baud` | serial | 115200 | ignored by native USB and `socket://` |
| `wifi_url` | wifi | none | must start with `ws://` |
| `wifi_idle_timeout` | wifi | 10 | seconds of silence before reconnecting |
| `ble_name` | ble | none | advertised device name |
| `ble_characteristic` | ble | none | UUID string, as written above |

Command line:

| Flag | Meaning |
|---|---|
| `--source` | `mock` (default), `serial`, `wifi`, `ble` |
| `--port` | web page port, default 5001 |
| `--serial-port` | serial port to use, overrides `config.json` |
| `--mock-tracking` | with `--source mock`, also send a moving sun target |
| `--list-ports` | list the serial ports this computer sees and which one auto-detect would pick, then exit |
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
| `[WARN] Dropped a message (not valid JSON)` | truncated or malformed message, or a `nan` from printf. Over BLE, check the MTU. |
| `Serial: ... being used by another program` | `idf.py monitor`, Arduino Serial Monitor, or another dashboard has the port. Close it. |
| `Serial: no ESP32 board found` | not plugged in, charge-only cable, or (Windows) driver not installed for a USB-serial chip |
| `Serial: found several possible boards` | two boards plugged in. Unplug one or pass `--serial-port`. |
| `Serial: no permission to open` (Linux) | run the `usermod` command it prints, then log out and back in |
| `[HINT] Serial port is open but the board is sending nothing` | press RST on the board; it may be in the bootloader |
| `[HINT] Serial ... no line contains sensor JSON` | the board prints, but not in a format from section 2. The last line is shown. |
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
| Serial cable unplugged, board reset or reflashed | retries every 2 seconds, re-detecting the port in auto mode |
| Another program has the serial port | says so once, connects as soon as it is released |
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
python tools/fake_esp32_ws.py --schema nested  # Jennifer's grouped shape
```

then set `"wifi_url": "ws://127.0.0.1:8765/ws"` and run
`python app.py --source wifi`. It's also a working example of what a
correct stream looks like.

Jennifer's simulated firmware, through serial. Wokwi can expose the
simulated board's serial port (documented by Wokwi, not yet tried by us):

1. In her `wokwi.toml`, under `[wokwi]`, add `rfc2217ServerPort = 4000`.
2. Switch JSON printing on in her validation app (`SY_CONSOLE_PRINT_JSON`).
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
3. Update rate. 5 to 10 messages per second looks smooth. The page
   handles far more (tested at 200 per second), but serial bandwidth and
   the ESP32's time are better spent elsewhere.
4. Whether the dashboard should show the light sensor and
   voltage/current. Right now it shows pitch, roll, power, and the
   optional targets.
5. Whether messages should carry a device timestamp.

## 8. Tested, and not tested

Tested against stand-ins: simulated serial ports (Jennifer's real line
format, the telemetry code above compiled with warnings as errors, a port
held by another program, a silent board, a board printing only logs, an
unplug, auto-detection, 50 full-size messages a second), a simulated
WebSocket server that went silent, closed, refused, sent binary frames,
batches, and oversize messages, a simulated BLE library, random and
malformed input, Python 3.9 and 3.13, and the page itself in headless
Chrome (desktop and phone widths, both themes, 200 messages a second,
CSV download).

Not tested: a real ESP32 or USB cable, how real boards respond to the
DTR/RTS sequence (simulated ports don't have those lines), a real BLE
radio, Wokwi's serial forwarding, Windows, or macOS. Expect the first
real connection to turn something up.

## 9. Not built yet

- Saving raw readings to disk (history, TinyML training data)
- Showing the health and fault information
- Displaying light, voltage, and current values
