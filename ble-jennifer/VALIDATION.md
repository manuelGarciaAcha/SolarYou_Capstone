# BLE validation

Results for `ble-jennifer`.

## Results

| Check | Result |
|---|---|
| C formatter, fragmentation and session model | 96 assertions passed with strict GCC warnings. Covers the five telemetry fields, valid/invalid rates, zero/null targets, message sizes and connection scheduling. |
| BLE server with simulated ESP/NimBLE APIs | 38 assertions passed. Covers registration, notifications, rate write/readback, fragmentation, reconnect handling, and fresh/stale readings across task timing and timer rollover. |
| BLE application bridge | 75 assertions passed in each mode. Covers completion-based power spacing, failure backoff with continuing telemetry, stale-field detection, nulls and real zero. |
| Shared application startup | 66 assertions passed in motor-off mode and 67 in full-control mode. Runs the actual startup code with simulated sensors/controller calls: a missing INA219 permits startup, while required IMU/light failures still stop full-control startup. |
| Power sensor and INA219 driver | 491 assertions passed using the actual driver, wrapper, health and math code with simulated Wire/BusIO. Covers setup/calibration failures, stopping at the first read failure, zero/scaled readings, timeout restoration, last-good timestamps and recovery from runtime faults. |
| Python suite | 64 tests run: 63 passed; one Linux-only permission test skipped on Windows. Covers initial retries, failed rate restoration with continued streaming and manual retry, real disconnections, cancellation, unsupported firmware, queued failures before newer requests, readback failures, whole/split batches, and missing/zero readings. |
| Dashboard JavaScript | 111 Node VM assertions passed. Covers same-rate retry after failed restoration, the neutral prompt without an invalid command, visible retry counts, error/unsupported states, stale success timers, and verifies the original dashboard script, calculations and CSV. |
| Browser preview | Generated-data preview passed: Verifying then confirmed 10 updates/second, continuing readings/CSV rows, no horizontal overflow, and no browser JavaScript errors. |
| Full-control compilation | Main application and BLE bridge source files compiled using the ESP32 compiler. A complete full-control firmware link was not tested. |
| Complete ESP32-S3 build | Compilation, linking and image generation passed. Application: 631,504 bytes in a 1,048,576-byte partition, approximately 40% free. Included binary uses motor-off mode. |

## Tested behavior

Telemetry contains `pitch`, `roll`, `power`, `pitch_target` and `roll_target`. Invalid or unavailable values are `null`; a real zero remains zero. Targets are copied from the existing controller after valid light updates in full-control mode. Motor-off mode sends null targets.

The BLE bridge publishes snapshots at most once every 100 ms. Power-read spacing is measured from completion: at least 100 ms after a healthy read and one second after a failed read. Fresh IMU/target snapshots continue during power backoff; values that have become stale are not relabeled fresh.

Runtime power reads temporarily use a 2 ms timeout per I2C transaction and restore the previous shared-bus timeout on every return path. Calibration/configuration failures remain invalid, while genuine zero stays numeric. This reduces INA-related waits; it does not establish a 20 ms whole-loop deadline. The existing IMU access and actual scheduler/bus timing still need hardware measurement.

In full-control mode, an INA219 initialization failure logs a warning and permits startup with `power:null`. The IMU and light-sensor initialization and calibration checks still apply. Motor-off startup behavior is unchanged. Runtime power-read failures also become `null`.

The dashboard confirms a rate change only after reading back the matching setting. A failed request reports its error before the next queued request starts, so an older failure cannot overwrite newer progress. The receiver reassembles split readings, clears unfinished messages after disconnect, and attempts to restore the last confirmed rate during the running dashboard session. A failed restore keeps a healthy connection streaming with a rate error and permits manual retry; real disconnections still reconnect. Simulated restore failures cover rejected writes, failed or timed-out readbacks, and mismatched settings without falsely confirming the rate.

## Evidence and reproduction

Firmware build instructions are in `README.md`. The bundled automated test helpers were verified on Windows and require GCC/G++, Node.js and Python with the dashboard dependencies. `tools/run_checks.ps1` accepts `-PythonPath` to select that Python environment. The test helpers and DLL-based preview have not been ported to Linux.

Packaged evidence is in `artifacts/checks.log`, `artifacts/simulation-trace.jsonl`, `artifacts/browser-results.json`, `artifacts/full-control-compile.json` and `artifacts/validation-results.json`.

The production patches were checked against their recorded firmware/dashboard revisions. The full source package also supplies separate test and generated-data preview helpers.

## Hardware testing still required

No board was flashed and no real Bluetooth link was tested. Generated readings and simulated ESP APIs do not verify sensor accuracy, power savings, motor stability or control timing.

On the team's hardware, check Bluetooth connection and actual message size, pitch/roll and power readings, all sending rates and confirmation, disconnect/reconnect behavior, and controller timing with BLE and INA219 polling enabled. Linux hardware operation also remains unverified.
