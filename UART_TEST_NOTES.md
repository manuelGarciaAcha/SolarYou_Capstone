# Jennifer UART telemetry test notes

## Choose the runtime mode first

The default build is **Sensor/dashboard test (motor outputs disabled)**.
It requires and calibrates the BNO085, reads the IMU, and sends the UART JSON,
but it never initializes the servos or calls the motor-command function. It
also reports the light array and INA219 separately. A missing or uncalibrated
light array is reported without stopping IMU telemetry, and the unused INA219
is optional until power telemetry is connected. This mode safely tests the
connection between the ESP32 and the dashboard.

To check or change the selection:

```powershell
idf.py menuconfig
```

Open **SolarYou application -> Runtime mode** and choose one of these:

- **Sensor/dashboard test (motor outputs disabled):** safe communication test.
- **Full integration (motor outputs enabled):** runs the full light-tracking
  and PID motor-control path. This mode should be selected once all hardware
  connections and GPIO assignments have been verified.

The firmware prints one of these messages at startup so the selected behavior
is visible in the serial monitor:

```text
SENSOR/DASHBOARD TEST MODE: motor outputs are disabled
FULL CONTROL MODE: motor outputs are enabled
```

Sensor/dashboard mode also prints a per-sensor report. For example:

```text
BNO085 IMU: PASS
Four-light array: PASS
INA219 power sensor: UNAVAILABLE (optional in this mode)
BNO085 IMU calibration: PASS
Four-light array calibration: PASS
```

The BNO085 is the only sensor that must pass for pitch/roll UART testing. A
light-array failure is reported as a warning, and a missing INA219 does not
block the UART loop while `power` remains `null`.

## UART output

Every 200 milliseconds, the ESP32 sends the latest pitch and roll readings to
Zack's dashboard in this format:

```json
{"pitch":1.23,"roll":-0.50,"power":null}
```

Each message ends on a new line so the dashboard can tell where one reading
ends and the next one begins. This gives the dashboard five readings per
second. Zack's dashboard skips the other startup and log messages from the
ESP32 and only uses these JSON messages.

Power is shown as `null` because the INA219 reading is not being sent to the
dashboard yet.

## Safe UART setup and test steps

1. Confirm all required hardware connections have been checked.
2. Confirm **Sensor/dashboard test (motor outputs disabled)** is selected.
3. Keep the external servo power disconnected during this first test as a
   second physical safety precaution.
4. Build and flash this branch for the exact ESP32 target.
5. Open the ESP-IDF monitor and confirm one JSON line appears about five times
   per second.
6. Confirm the line is readable at 115200 baud.
7. Close the ESP-IDF monitor so the dashboard can use the serial connection.
8. Copy `dashboard/config.example.json` to `dashboard/config.json`. Enter the
   serial port assigned by the computer, such as `COMx` on Windows or
   `/dev/ttyACM1` on Linux.
9. From the repository's main folder, run
   `python dashboard/app.py --source serial` and open the dashboard. On Linux,
   use `python3` instead if the `python` command is not available.

## Full integration setup and test steps

For Rudy's Heltec WiFi LoRa 32 V4 build, this branch sets servo channel 0 to
GPIO41 and servo channel 1 to GPIO42. These replace the temporary GPIO33 and
GPIO32 assignments in the original shared README. Confirm that the physical
wiring matches GPIO41 and GPIO42 before connecting servo power.

1. Confirm all sensor and servo connections, GPIO assignments, power setup,
   and mechanical movement have been checked.
2. Confirm **Full integration (motor outputs enabled)** is selected.
3. Build and flash the firmware for the exact ESP32 target while the external
   servo power is still disconnected.
4. Place the mechanism in a safe starting position and make sure there is room
   for both axes to move before connecting the servo power.
5. Watch the ESP-IDF monitor for sensor, calibration, or motor errors. The
   servo power should be disconnected immediately if the mechanism moves in
   the wrong direction or reaches a limit.
6. Slowly tilt the base and move the light source to check stabilization and
   light tracking in both directions.
7. After checking the movement, close the ESP-IDF monitor, enter the assigned
   serial-port value in `dashboard/config.json`, and run the dashboard using
   the same setup in steps 8 and 9 above.

## Important limitations

The BNO085 must be connected and calibrate successfully before the ESP32 can
send pitch and roll to the dashboard. If the sensor is missing or calibration
fails, the UART messages will not start. In sensor/dashboard test mode, the
light array and INA219 are checked and reported but remain optional. The
default full-integration setup expects the BNO085, light array, and INA219 to
all pass their startup checks.

If the IMU stops giving valid readings after telemetry has already started,
the current code reports the error but continues sending the last saved pitch
and roll values. If an IMU error appears and the dashboard readings stop
changing, those readings may be old rather than new sensor measurements.
