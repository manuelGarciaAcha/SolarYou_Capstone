# SolarYou — Active Stabilization and Solar Tracking Platform (ASSTP)

**Project:** Senior Design II | **Status:** Rev1 sensor/control integration and bench validation | **Snapshot reviewed:** October 9, 2026

SolarYou is a two-axis photovoltaic platform intended to reject wave-induced pitch/roll disturbances while adjusting its orientation toward the strongest available sunlight. This repository contains the ESP32 firmware, sensor interface, motor control, PID controller, and supporting vendor libraries for the prototype.

> **Development status:** The firmware has an integrated initialization path and a software path from IMU measurements and light-sensor errors to servo commands. This is **not yet evidence of successfully calibrated, stable, or safe closed-loop operation on the physical gimbal**. Electrical-power telemetry and the dashboard transport are not connected in the application snapshot reviewed here.

## Current implementation status

| Subsystem | Current state |
|---|---|
| ESP-IDF project and reusable components | Organized into `main/` and `components/`; compile/link configuration established |
| Board configuration | Separate ESP32 and ESP32-S3 mappings in `board_config.h`; **S3 pins require hardware validation** |
| Sensor abstraction | C-facing `sensor_adapter_*` interface implemented using C++/Arduino sensor drivers |
| BNO085 IMU | Driver, initialization, neutral capture, calibrated pitch/roll read, health information; called by stabilization |
| Four-channel light array | Driver, initialization, neutral capture, normalized directional errors, health information; called by light task |
| INA219 power monitoring | Driver and sensor API implemented; **not yet connected to `curr_state` in the main loop** |
| PID stabilization | Independent roll/pitch controllers, derivative filtering, conditional anti-windup, command limits, reset/tune interface |
| Servo actuation | Two-servo ESP-IDF component interface and software angle clamps |
| Solar tracking | Incremental tracking targets: `target += track_k * normalized_light_error`; **target bounds not yet enforced** |
| Scheduling | Sequential time-checked loop in `app_main()`; **not yet separate FreeRTOS tasks** |
| System state | Persistent roll/pitch readings, tracking targets, PID corrections, power fields and operating mode |
| Dashboard/WebSocket transport | Not implemented in the uploaded `main/` snapshot; sensor JSON support exists within `sensor_service` |
| Hardware verification | No complete end-to-end test evidence included in this snapshot |

## Software architecture

```text
ESP_SolarYou/
├── main/
│   ├── ESP_SolarYou.c          # Startup, sequential scheduling, shared application state
│   ├── stabilization_task.c/.h # IMU read -> roll/pitch PID -> servo commands
│   └── light_task.c/.h         # Light read -> incremental target update
├── components/
│   ├── board_config/          # Target-specific GPIO assignments
│   ├── motor_control/         # Servo initialization, commands and cleanup
│   ├── pid_stabilization/     # Reusable PID implementation
│   ├── system_state/          # State representation and reset
│   ├── sensor_service/        # Arduino-backed sensor drivers, C API, health, JSON
│   └── adafruit_*/            # Third-party Arduino library components
├── scripts/                   # Pinned Adafruit library fetch scripts
├── main/idf_component.yml    # ESP-IDF component manager dependencies
├── dependencies.lock
├── sdkconfig.defaults
└── CMakeLists.txt
```

### Startup sequence

`app_main()` currently performs the following operations:

1. Reset `curr_state` to its initial values.
2. Initialize the sensor service (`sensor_adapter_init()`); the adapter initializes the Arduino runtime and the enabled sensor drivers.
3. Capture the IMU neutral reference (50 samples).
4. Capture the light-array neutral reference (50 samples).
5. Initialize motors and independent roll/pitch PID controllers.
6. Set `SYSTEM_BASIC_CONTROL` and enter the sequential scheduler.

The default sensor validation configuration enables IMU, light and power paths; **initialization may fail if any required sensor, including the INA219, is absent**. Neutral capture can also fail when physical conditions do not satisfy the stability/light-quality checks.

### Control and tracking flow

```text
LIGHT UPDATE (nominally every 500 ms)
    sensor_adapter_read_light()
        -> normalized roll_error, pitch_error
        -> curr_state.roll_light  += track_k * roll_error
        -> curr_state.pitch_light += track_k * pitch_error

STABILIZATION UPDATE (nominally every 20 ms)
    sensor_adapter_read_imu()
        -> calibrated measured roll/pitch
        -> PID(measured_angle, stored target_angle)
        -> servo_center + target_angle + PID_correction
        -> command_motor_angle()
        -> expose measurements/corrections through stab_output_t
        -> update curr_state in app_main()

TELEMETRY SLOT (nominally every 200 ms)
    Placeholder: power/transport work not yet connected
```

**Important:** `curr_state.roll_light` and `curr_state.pitch_light` currently represent persistent **tracking target angles in degrees**, despite their names. `curr_state.roll_cmd` and `curr_state.pitch_cmd` represent the **PID corrections**, not the final physical servo positions.

The tracking update is a slow *incremental controller*: `target[k+1] = target[k] + track_k * light_error[k]`. It does **not** interpret `light_error` directly as a sun angle. During the intervening stabilization cycles, the PID repeatedly follows the same stored target; it does not add the light correction 25 more times.

### Configured update periods

| Operation | Constant | Nominal period | Nominal frequency |
|---|---|---:|---:|
| Stabilization | `STAB_TASK_DT_US` | 20,000 µs | 50 Hz |
| Light tracking | `LIGHT_TASK_DT_US` | 500,000 µs | 2 Hz |
| Telemetry slot | `TELEM_TASK_DT_US` | 200,000 µs | 5 Hz (functionality pending) |

These are **scheduler targets, not measured guarantees**. `app_main()` uses `esp_timer_get_time()` and `vTaskDelay(1)`; blocking sensor acquisition can delay later updates. The PID currently receives a fixed nominal `dt = 0.020 s` rather than elapsed time measured between actual samples.

## Hardware and pin configuration

The authoritative GPIO map is `components/board_config/include/board_config.h`. Do not duplicate pin definitions in the sensor drivers.

| Signal | Classic ESP32 configuration | ESP32-S3 configuration |
|---|---|---|
| Shared I²C SDA / SCL | GPIO21 / GPIO22 | GPIO47 / GPIO48 (verify board) |
| Servo channels 0 / 1 | GPIO33 / GPIO32 | GPIO33 / GPIO32 (**temporary; verify module**) |
| Light top-left / top-right | GPIO34 / GPIO35 | GPIO3 / GPIO4 (verify board) |
| Light bottom-left / bottom-right | GPIO36 / GPIO39 | GPIO5 / GPIO6 (verify board) |

- **BNO085** uses I²C address `0x4A` in `component_config.h`.
- **INA219** uses I²C address `0x40` and shares the same SDA/SCL bus.
- The INA219's measurement terminals are not ESP32 GPIO pins.
- Servo control uses the `espressif/servo` component. Servo power, common ground, voltage, current budget and mechanical limits must be verified with the actual hardware.

> The current ESP32-S3 pin assignments are not a validated universal pinout: certain GPIOs may be reserved by the selected module or unsuitable for connected hardware. Check the precise module and development board before wiring.

## Dependencies and building

- **ESP-IDF:** `5.5.5`
- **Arduino-ESP32 component:** `3.3.11`
- **Espressif servo component:** `^1.0.0`
- **Adafruit BNO08x:** `1.2.7`
- **Adafruit INA219:** `1.2.3`
- **Adafruit BusIO:** `1.17.4`
- **Adafruit Unified Sensor:** `1.1.15`

Install/activate ESP-IDF 5.5.5 first. If the Adafruit `components/adafruit_*/vendor/` directories are missing, run the repository's fetch script (it verifies pinned versions and will not overwrite a mismatched existing vendor directory):

```bash
bash scripts/fetch_arduino_libs.sh
```

From the repository root, select your **actual** device target when setting up or switching boards:

```bash
idf.py set-target esp32      # Classic ESP32-WROOM board
# or: idf.py set-target esp32s3
idf.py build
```

To flash and view logs, with the appropriate USB serial port and connected hardware:

```bash
idf.py -p /dev/ttyUSB0 flash monitor
```

The serial-port path is only an example. `idf.py set-target` changes the local target configuration and resets the build directory; it is not required every time you compile. Preserve meaningful custom menuconfig settings in `sdkconfig.defaults` when preparing a reproducible team configuration. The project uses its own `app_main()` rather than Arduino `setup()`/`loop()`; Arduino initialization happens within `sensor_adapter_init()`.

## Considerations While Testing

This is the active engineering issue register for **Rev1 bench testing**. Items marked **Confirmed** are observable in the current source; items marked **Validate** require physical measurement or a controlled experiment. **Deferred** items are more relevant to later releases, although basic hardware safety still applies during testing.

### A. Confirmed implementation issues

| ID | Priority | Observation | Testing consideration / expected follow-up |
|---|---|---|---|
| C01 | **High** | `light_update()` accumulates tracking targets with `+=` but applies no target-angle clamp. | Define achievable roll/pitch target bounds before extended powered tracking tests; observe behavior at limit. |
| C02 | Medium | PID receives fixed `dt = 0.020 s`; loop elapsed time is not measured. | Log actual intervals. Use measured elapsed `dt` if variability is meaningful. |
| C03 | **High** | PID correction clamps to ±50°, and motor commands clamp to 40–140°, but the combined `servo_center + target + correction` command is not accounted for in PID anti-windup. | Determine achievable command space and check clamping/integrator behavior under saturation. |
| C04 | Medium | The scheduler sets `SYSTEM_ERROR` but continues scheduling normally. | Decide what a sensor/motor error should mean operationally; do not rely on the enum alone as a safety shutdown. |
| C05 | Low | `main/light_task.h` uses `esp_err_t`, `system_state_t`, and `stab_config_t` without including their defining headers. | Make the header self-contained so it can be included independently. |
| C06 | Medium | INA219 `sensor_adapter_read_power()` exists, but the power-telemetry slot is commented out and `read_power_placeholder()` returns zero. | Connect power samples to `curr_state` and confirm V/A/W units and averaging. |
| C07 | Medium | `sensor_json` serialization exists, but no dashboard/WebSocket transport is connected in the uploaded main application. | Agree on a payload/transport contract with the dashboard team; test the live data path after telemetry wiring. |
| C08 | Low | `stabilization_init()` does not clean up initialized servo resources if a subsequent PID initialization fails. | Add cleanup on unsuccessful initialization paths. |
| C09 | Low | `command_motor_angle()` does not explicitly reject calls before both servo handles have been initialized. | Check initialization state in the motor command path. |
| C10 | Deferred | `sensor_api.h` documents shared I²C access but doesn't provide RTOS-level arbitration. | Serialize IMU/INA219 access if introducing concurrent FreeRTOS tasks. Current sequential loop does not simultaneously access the bus. |

### B. Assumptions and behavior to validate experimentally

| ID | Priority | Question | Suggested test / evidence |
|---|---|---|---|
| T01 | **High** | Does light-neutral capture subtract a real sun-direction error? | Under known directional illumination, record raw four-channel ADC values and normalized errors **before and after** neutral capture. Calibration stores the initial imbalance as a reference. |
| T02 | **High** | Do light-error signs and IMU/PID correction signs command the correct physical direction? | Move/illuminate one axis at a time with conservative servo travel and verify whether error decreases. |
| T03 | **High** | Is `servo_center + target + PID_correction` a valid relationship for the actual gimbal? | Compare commanded servo angles to measured panel angle; evaluate linkage ratios, offsets, cross-axis coupling and mounting location. |
| T04 | High | Are PID gains (`Kp = Ki = Kd = 0.5`) stable and suitably tuned? | Record response to controlled roll/pitch disturbances: rise/settling time, overshoot, steady-state error and oscillation. Tune axes separately. |
| T05 | High | Can IMU acquisition meet the desired 20 ms stabilization period? | Measure typical, worst-case and timed-out IMU read durations. Driver timeout is **up to 250 ms**, not necessarily normal latency. |
| T06 | Medium | Is `track_k = 2.0` suitable at the 500 ms light-update interval? | Measure target evolution and convergence from known light offsets; vary gain and update period independently. |
| T07 | High | Can the required IMU and light neutral references be captured reliably at startup? | Test stationary/level startup, gently moving startup, dark/saturated illumination, and already-misaligned sunlight. Record calibration failures. |
| T08 | High | Are the proposed board GPIO assignments electrically valid? | Verify ESP32-S3 GPIO reservations and all actual wiring against the precise module/board datasheet. |
| T09 | **High** | What are the mechanical limits and safe powered operating range? | Verify servo centers, real travel, collision clearances, available torque/current and power supply before mounting the panel or running prolonged closed loop. |
| T10 | Medium | Does the application behave predictably when sensors are unavailable or reset? | Disconnect/reconnect sensors during controlled low-risk tests; observe validity flags, latched faults, log output, and restart/recalibration behavior. |

### Suggested Rev1 bench test sequence

1. **Unpowered inspection:** Confirm GPIO map, grounds, separate servo supply, gimbal clearance and a way to disconnect actuator power.
2. **Sensor-only startup:** Confirm IMU/light/INA219 initialization and neutral capture; log validity and raw/centered measurements with motors disconnected.
3. **Servo-only operation:** Verify each axis and polarity independently with conservative travel limits and no closed-loop disturbance rejection.
4. **Stabilization-only testing:** Hold tracking targets at zero; log IMU attitude, PID correction, actuator command and actual response to known tilts.
5. **Light tracking testing:** Confirm calibration behavior and normalized-error signs; use a low gain and bounded targets; measure convergence and oscillation.
6. **Combined testing:** Enable both axes, introduce controlled base motion and illumination changes, compare tracking and stabilization performance to a fixed-orientation baseline.
7. **Performance recording:** Record update times, missed deadlines, angle error, settling time, light imbalance, electrical power and failures.

**Recommended immediate prerequisites for powered closed-loop testing:** bound tracking targets (C01); establish safe mechanical travel (T09); verify sensor calibration and correction directions (T01–T03). Most remaining items should be evaluated with data rather than speculative code changes.

## Planned work and Rev2 possibilities

**Remaining Rev1 integration:** sensor/power telemetry into `curr_state`, dashboard interface, calibration and control validation, PID/tracking tuning, measured stabilization and tracking performance.

**Potential Rev2 work (test-driven):** independent FreeRTOS tasks, I²C synchronization and deadline monitoring, enhanced supervisor/degraded modes, measured-time PID integration, refined light calibration, improved tracking reference generation, richer telemetry and regression tests. A move to native ESP-IDF sensor drivers should be driven by measured timing/resource needs rather than assumed Arduino overhead.

## Development notes

- `CONFIG_FREERTOS_HZ=1000` and `CONFIG_HTTPD_WS_SUPPORT=y` appear in `sdkconfig.defaults`, but enabling WebSocket support does **not** itself implement a dashboard server.
- No root README was present in the October 9 reviewed ZIP; this file documents that snapshot. Keep it updated as features are built and tests are performed.
- A successful `idf.py build` verifies compilation and linking; it does not establish correct wiring, timing, calibration, or closed-loop performance.
