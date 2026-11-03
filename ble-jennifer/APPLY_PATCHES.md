# BLE patch handoff

The team can approve individual changes. Applying a supplied patch includes all the changes in that file. If only some changes are approved, revise the patches to include only those changes before applying them.

Keep dependent firmware and dashboard changes together: split-message sending needs dashboard reassembly, and dashboard rate selection/confirmation needs the corresponding firmware support.

Firmware main: d4b2de907897ce507fc3196b2f484c0767f396df
Dashboard zack: d568ba54187871d1f962b6d4d536e853b8c70e1b

Use firmware.patch for the ESP32 firmware and dashboard.patch for the dashboard. Apply each patch to its corresponding project files at the version listed above.

Use git apply --check before git apply. Firmware builds must include sdkconfig.ble.defaults alongside the original sdkconfig.defaults to enable the required NimBLE SDK settings.

Changes to existing code:

- ESP_SolarYou/main/ESP_SolarYou.c: one include and four BLE calls, plus a full-control startup change that warns and continues with power:null if INA219 initialization fails. The IMU and light-sensor startup checks remain required.
- ESP_SolarYou/main/CMakeLists.txt: registers the three new BLE sources and SDK bt/nvs_flash libraries.
- ESP_SolarYou/components/sensor_service/power_sensor.cpp: limits runtime INA transaction waits to 2 ms each, restores the previous shared-bus timeout, stops at the first read failure, and checks setup success.
- ESP_SolarYou/components/adafruit_ina219/vendor/Adafruit_INA219.cpp: propagates calibration/configuration failures and initializes failed-read return values so invalid readings are not accepted as valid zero.
- dashboard/app.py: replaces the BLE receiving loop with the new receiver and adds rate request/status handling, including rate readback, split messages and reconnection. The receiver retries temporary initial rate-check failures up to three total reads while live readings continue. Failed rate restoration after reconnect leaves a healthy connection streaming, shows a rate error, and allows manual retry.
- dashboard/templates/index.html: adds the sending-rate selector/status and loads telemetry-rate.js. An unconfirmed restore failure shows "Choose a rate" so the same rate can be selected again directly.

The BLE bridge publishes snapshots at most once every 100 ms. Healthy runtime power reads wait at least 100 ms after the previous read completes; failed reads wait one second before another attempt while IMU/target telemetry continues. Stale IMU/target values remain unavailable. Check the 2 ms transaction timeout and actual control-loop timing on hardware. Existing controller target values are copied. As mentioned, the code needs to be reviewed by the team before integration into main. See README.md for installation and build steps; the build command needs an installed ESP-IDF 5.5.5 toolchain.

Also as mentioned, if a reading is split across Bluetooth messages, the dashboard needs the included receiver code to join the pieces before it can display the reading. Use the corresponding firmware and dashboard changes for the approved features. The full source package includes separate tests and a generated-data preview. See its VALIDATION.md for results and hardware checks still required.
