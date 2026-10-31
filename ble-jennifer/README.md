# ble-jennifer

Download and extract [ble-jennifer.zip](ble-jennifer.zip) for the complete source and test files.

BLE telemetry for SolarYou with adjustable sending rate, applied-rate confirmation, reconnection, and split-message support.

Related documents: [BLE protocol](BLE_PROTOCOL.md), [patch instructions](APPLY_PATCHES.md), and [validation results](VALIDATION.md).

## Setup

**Install ESP-IDF 5.5.5 and its ESP32-S3 tools before building the firmware.** If this version is already installed, reuse it. Follow Espressif's [Windows setup](https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32s3/get-started/windows-setup.html) or [Linux setup](https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32s3/get-started/linux-macos-setup.html).

Extract the project into a folder without spaces, such as `C:\esp\ble-jennifer` or `~/esp/ble-jennifer`. Open the installed ESP-IDF terminal, run `idf.py --version`, and confirm it reports 5.5.5. From the extracted project folder, run:

```powershell
cd firmware/ESP_SolarYou
```

Then build with the BLE settings:

```powershell
idf.py -B build-ble -D CMAKE_NINJA_FORCE_RESPONSE_FILE=ON -D IDF_TARGET=esp32s3 -D "SDKCONFIG=build-ble/sdkconfig" -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.ble.defaults" build
```

Keep the existing `main/idf_component.yml` and `dependencies.lock` files with the source so ESP-IDF uses the project's recorded library versions. Missing libraries are [downloaded automatically during configuration](https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32s3/api-guides/tools/idf-component-manager.html), so the first build needs internet unless they are already cached. BLE uses NimBLE supplied with ESP-IDF.

The build creates firmware files; uploading them to the board is a separate step. The default remains motor-off testing. Select the existing full-control mode in menuconfig when integrating motor operation.

In full-control mode, an INA219 startup failure logs a warning and allows startup to continue with `power:null`. The IMU and light sensors must still initialize successfully. Motor-off testing already allows an unavailable INA219.

**To run only the dashboard, ESP-IDF is not needed.** Use Python 3.10+ and a BLE-capable adapter. From the project root, install the Python packages:

```powershell
python -m pip install -r dashboard/dashboard/requirements.txt
```

Copy `dashboard/dashboard/config.example.json` to `config.json` in that same directory if you do not already have one. From `dashboard/dashboard`, run:

```powershell
python app.py --source ble
```

Open `http://localhost:5001/`.

## Linux setup

Use an ESP-IDF 5.5.5 Linux terminal (or source your installation's `export.sh`), then run the build command above. Install system prerequisites for your distribution using [Espressif's Linux setup guide](https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32s3/get-started/linux-macos-setup.html).

For the dashboard, use Python 3.10+ for current Bleak releases, a BLE-capable adapter, and BlueZ 5.55+ with Bluetooth enabled and D-Bus access. Check the adapter with `bluetoothctl show`. See [Bleak's Linux requirements](https://bleak.readthedocs.io/en/latest/backends/).

From the project root, create a dashboard environment separately from the ESP-IDF shell:

```bash
python3 -m venv .venv
source .venv/bin/activate
python -m pip install -r dashboard/dashboard/requirements.txt
python dashboard/dashboard/app.py --source ble
```

Create `config.json` as described above. BLE identifiers and firmware settings are the same on Linux.

For USB flashing/monitoring, use the actual `/dev/ttyACM*` or `/dev/ttyUSB*` device instead of a Windows COM port. Your account needs access to that device's group, commonly `dialout` or `uucp` on Arch; log in again after changing group membership. These USB permissions are separate from BLE access. See [Espressif's serial-permission instructions](https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32s3/get-started/establish-serial-connection.html#adding-user-to-dialout-or-uucp-on-linux).

The bundled PowerShell helpers, Windows compiler-check helper, and DLL-based preview/test builds are Windows-specific. The real BLE dashboard does not need that DLL. Linux hardware operation has not yet been verified for this project.

## BLE interface

| Setting | Value |
|---|---|
| Device name | `SolarYou-ASSTP` |
| Service UUID | `47e8841f-04c9-4ae2-af68-a7b6f4eb293d` |
| Readings notification UUID | `f2d2680c-66bc-4e24-8eac-d8013a99320b` |
| Added rate read/write UUID | `f2d2680c-66bc-4e24-8eac-d8013a99320c` |

```json
{"pitch":1.25,"roll":-2.0,"power":null,"pitch_target":8.0,"roll_target":-4.0}
```

Angles are degrees; power is watts. Unavailable values are `null`; zero is valid. Targets are the existing controller setpoints in the same startup-neutral reference as the IMU readings. They remain `null` in motor-off mode or after an invalid light update.

## Rate control and receiver

Supported rates: **1, 2, 5, 10 updates/second**; default **5**. This changes outgoing telemetry, not IMU/PID sampling.

On connection, the dashboard retries a temporarily failed rate check up to two more times, showing "Checking ESP32 rate (1/3)..." while live readings continue. If all three attempts fail, rate control stays disabled with an error until Bluetooth reconnects. Firmware without the rate setting is reported as unsupported.

When a user selects 5 updates/second, the dashboard sends `{"set_rate_hz":5}` to the ESP32 through the added Bluetooth rate setting. It then reads the setting back; `{"rate_hz":5}` means the ESP32 reports that 5 updates/second is applied. The dashboard handles these messages automatically and confirms only when the returned setting matches the request. It attempts to restore the last confirmed rate after reconnecting during the running session. If that setting request fails while Bluetooth remains connected, readings continue and a rate error is shown. When the rate is unconfirmed, the dropdown shows "Choose a rate" so the same rate can be selected again directly.

Whole JSON is sent when it fits the negotiated Bluetooth message size; otherwise it is split into smaller pieces. The included dashboard receiver code joins those pieces into one complete reading before displaying it and handles rate replies separately. Review the firmware and dashboard changes together. Exact framing is in [BLE_PROTOCOL.md](BLE_PROTOCOL.md).

## Integration and validation

Apply the matching firmware and dashboard patches to the revisions listed in the [patch instructions](APPLY_PATCHES.md). Firmware integration adds one include and four BLE calls in `ESP_SolarYou.c`, the full-control INA219 startup warning/continue behavior, and CMake module/library entries.

### Changes to existing code

These are the six existing production files changed by the patches:

| Existing file | What changed |
|---|---|
| `firmware/ESP_SolarYou/main/ESP_SolarYou.c` | Adds one include and four BLE calls to start BLE and pass readings/targets into it. Full-control startup warns and continues with `power:null` if INA219 initialization fails; IMU and light-sensor startup checks remain required. |
| `firmware/ESP_SolarYou/main/CMakeLists.txt` | Registers the three new BLE source files and the SDK's `bt` and `nvs_flash` libraries so they are built into the firmware. |
| `firmware/ESP_SolarYou/components/sensor_service/power_sensor.cpp` | Checks setup success, temporarily limits runtime power transactions to 2 ms each, restores the shared-bus timeout, and stops at the first failed read. |
| `firmware/ESP_SolarYou/components/adafruit_ina219/vendor/Adafruit_INA219.cpp` | Checks calibration/configuration write results and preserves failed-read status so invalid readings are not accepted as valid zero. |
| `dashboard/dashboard/app.py` | Replaces the BLE receiving loop with the new receiver for split messages, reconnection, rate commands, and rate readback. Adds the dashboard's rate request/status events. |
| `dashboard/dashboard/templates/index.html` | Adds the sending-rate selector and status text, and loads the new `telemetry-rate.js` file. |

New firmware files are `ble_app.c/.h`, `ble_telemetry.c/.h`, `ble_protocol.c/.h`, and `sdkconfig.ble.defaults`. New dashboard code is `ble_transport.py` and `static/js/telemetry-rate.js`.

**Power reads and timing:** snapshots publish at most once every 100 ms. Healthy power reads wait at least 100 ms after completion; failed reads wait one second before another attempt while IMU/target telemetry continues. Runtime power transactions use a temporary 2 ms timeout, then restore the previous bus setting. These settings need checking on the real board and do not guarantee a 20 ms controller cycle. Targets come from the existing controller values.

As mentioned, the code needs to be reviewed by the team before integration into main. Also as mentioned, if a reading is split across Bluetooth messages, the dashboard needs the included receiver code to join the pieces before it can display the reading.

Build and automated checks passed; real-board Bluetooth, reading accuracy, and control timing still need testing. Detailed results are in [VALIDATION.md](VALIDATION.md).
