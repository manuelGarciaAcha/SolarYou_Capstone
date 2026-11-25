# BLE and USB changes

This update connects BLE to the current sensor/control application and uses
Zack's existing dashboard. The firmware reuses the readings already collected
by the program. Use the firmware and `dashboard/` from this checkout together.

**Start here:** [Major code changes](#major-code-changes-implemented),
[firmware configuration](#configure-and-flash-the-firmware), and
[dashboard configuration](#configure-the-dashboard).

### Major code changes implemented

| Change | Files | What changed and why |
| --- | --- | --- |
| BLE integration with the current application | `ESP_SolarYou.c`, `ble_app.c`, `ble_app.h` | Starts BLE after the required sensor startup checks and forwards the existing IMU, power, and controller-target readings. Bluetooth does not read the INA219 separately or change the sensor/control-loop periods. |
| Complete BLE data fields | `ble_protocol.c`, `ble_protocol.h` | Adds voltage, current, and average power to the existing BLE fields. Each packet includes `pitch`, `roll`, `pitch_target`, `roll_target`, `voltage`, `current`, `power`, and `avg_power`. Invalid or stale readings are `null`; valid zero values remain zero. Full-control targets come from the existing light-tracking setpoints. |
| BLE packet-size fix | `ble_telemetry.c`, `ble_telemetry.h` | Uses the live negotiated ATT MTU instead of the cached connection value. This avoids unnecessary fragmentation; smaller connections still use the existing split-message format. |
| USB IMU validity fix | `system_state.h`, `system_state.c`, `ESP_SolarYou.c`, `power_n_telemetry.c` | Adds and initializes `imu_valid`, updates it after each IMU/control update, and checks it before sending USB angles. Failed or nonfinite updates send null pitch/roll instead of repeating the last measured angles. Numeric angles resume after recovery, while valid power data continues. |
| Consistent average-power field | `power_n_telemetry.c` | Changes the USB key from `avg power` to `avg_power` for both valid and null readings, matching BLE and the dashboard's standard field name. |
| Windows BLE callback handling | `dashboard/ble_transport.py` | Adds a 50 ms timer to keep queued Bluetooth callbacks moving if the Windows event-loop wakeup fails. The timer is stopped when the receiver exits. |
| BLE build configuration | `CMakeLists.txt`, `Kconfig.projbuild`, `sdkconfig.ble.defaults` | Includes the BLE modules and Bluetooth/NVS dependencies, adds the BLE telemetry option, and supplies the NimBLE defaults for the existing real-sensor modes. |

Firmware files above are under `ESP_SolarYou/main/`, except `system_state.h` and
`system_state.c`, which are in `ESP_SolarYou/components/system_state/`, and
`sdkconfig.ble.defaults`, which is in `ESP_SolarYou/`.

Angles are degrees, voltage is volts, current is amps, and power values are
watts. The existing 1, 2, 5, and 10 Hz BLE rate settings, readback confirmation,
rate restoration during a running dashboard session, and split-message framing
are retained. Sensor startup requirements, calibration, PID settings, and motor
control are unchanged.

### USB behavior after an IMU update failure

USB continues sending messages, but `pitch` and `roll` are `null` until an IMU
update succeeds again. The dashboard shows unavailable angles and saves blank
angle cells in the CSV. Valid power readings continue independently. No new
dashboard setting is needed for this fix. This changes data validity.

### Configure and flash the firmware

Use ESP-IDF 5.5.5 with the ESP32-S3 target. The existing board configuration uses
16 MB flash. From `ESP_SolarYou/` in an ESP-IDF terminal on Windows or Linux,
start with a new build folder so the BLE defaults are loaded:

```text
idf.py -B build-ble -D IDF_TARGET=esp32s3 -D SDKCONFIG=build-ble/sdkconfig -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.ble.defaults" menuconfig
```

Under **SolarYou application → Runtime mode**, select:

- **Sensor/dashboard test:** real sensors, motor outputs disabled.
- **Full integration:** real sensors and motor outputs enabled.

Keep the platform level and still during startup because the IMU captures its
neutral reference. The existing sensor startup checks, calibration, PID settings,
and motor-control behavior are unchanged. BLE starts advertising after the
required startup checks pass; check the USB log if it never appears.

Build and flash, replacing `PORT` with the board's port (`COM10`,
`/dev/ttyACM0`, etc.). Close any serial dashboard or monitor first:

```text
idf.py -B build-ble build
idf.py -B build-ble -p PORT flash
```

### Configure the dashboard

Use this checkout's `dashboard/` folder. Run these setup commands from that
folder; they preserve an existing `config.json`.

**Windows PowerShell:**

```powershell
py -3 -m venv .venv
.\.venv\Scripts\python.exe -m pip install -r requirements.txt
if (-not (Test-Path config.json)) { Copy-Item config.example.json config.json }
```

**Linux terminal:**

```bash
python3 -m venv .venv
./.venv/bin/python -m pip install -r requirements.txt
if [ ! -f config.json ]; then cp config.example.json config.json; fi
```

The BLE settings in `config.json` should be:

```json
{
  "ble_name": "SolarYou-ASSTP",
  "ble_characteristic": "f2d2680c-66bc-4e24-8eac-d8013a99320b",
  "ble_rate_characteristic": "f2d2680c-66bc-4e24-8eac-d8013a99320c"
}
```

Leave other settings in an existing `config.json` as needed. Bluetooth must be
on; Linux uses the BlueZ service. Run the command for this computer from `dashboard/`:

Windows:

```powershell
.\.venv\Scripts\python.exe app.py --source ble
```

Linux:

```bash
./.venv/bin/python app.py --source ble
```

Open `http://localhost:5001`. These commands call the environment's Python
directly, so no separate activation step is needed.

The [dashboard README](dashboard/README.md) documents Zack's dashboard.
The [integration guide](dashboard/ESP32_INTEGRATION.md) covers the other
connections, and the [BLE protocol](dashboard/BLE_PROTOCOL.md) documents the
data fields, identifiers, rate setting, and split messages.
