# How I run the ESP-IDF 5.5 Wokwi sensor tests

I use these Wokwi tests to check one sensor group at a time, or all of them together, without needing the physical sensors connected. The tests use the
same C-compatible functions, snapshot, calculations, health states, and faults as the hardware build.

The knobs and buttons in Wokwi stand in for light, IMU, and power readings. This lets me check how the code reacts to different values and faults. It
does NOT check real I2C communication, accuracy, wiring, electrical noise, or whether the power setup is stable. That remains to be implemeted and tested.

## Available simulations

| Mode | Build command | Diagram | What is compiled and reported |
| --- | --- | --- | --- |
| All sensors | `.\scripts\build_wokwi.ps1` | `diagram.json` | IMU, light, and power |
| Light only | `.\scripts\build_wokwi.ps1 -Mode light` | `simulations/light_only/diagram.json` | Four-light array only |
| IMU only | `.\scripts\build_wokwi.ps1 -Mode imu` | `simulations/imu_only/diagram.json` | IMU application mock only |
| Power only | `.\scripts\build_wokwi.ps1 -Mode power` | `simulations/power_only/diagram.json` | Power application mock only |

Each option has its own build folder and `wokwi.toml`, so building one won't replace another. If I choose a single sensor test, the program doesn't
start, read, or print the other sensors. `selected_valid=1` means every sensor included in THAT test is valid.

ESP-IDF might still compile some shared Adafruit library files because they are listed as dependencies. The simulations do not call the real BNO085 or INA219 drivers.

## Build and start

1. Open the `SolarYou_Sensor` folder in VS Code.
2. Open a PowerShell terminal there.
3. Pick a test from the table and run its build command.
4. Wait until the build says it finished.
5. Open the `diagram.json` for that same test.
6. Press `F1` and choose Wokwi: Start Simulator.
7. Check that the terminal shows the right `VALIDATION MODE`, then wait for
   `sensor_init=1 sensor_calibration=1`.

Leave the knobs around the middle while the simulation starts. That gives the IMU and light tests a reasonable neutral starting point.

The code checks readings every 100 ms so the timeout and stale data checks still work. It only prints the usual update once a second, but it prints
right away when a sensor becomes invalid, changes state, or gets a new active fault.

I left the full JSON out of the normal terminal view so the output is easier to follow. If you want to see it, change `SY_CONSOLE_PRINT_JSON` from `0` to
`1` in `components/sensor_service/include/component_config.h`, then rebuild and restart the test. The JSON code is still included and has its own test
even when it isn't printed.

## Controls

| Control | GPIO | Simulated range | Present in |
| --- | ---: | --- | --- |
| Light top-left | 3 | `0..4095` ADC counts | Light/all |
| Light top-right | 4 | `0..4095` ADC counts | Light/all |
| Light bottom-left | 5 | `0..4095` ADC counts | Light/all |
| Light bottom-right | 6 | `0..4095` ADC counts | Light/all |
| IMU pitch | 7 | `-45..+45` degrees | IMU/all |
| IMU roll | 8 | `-45..+45` degrees | IMU/all |
| PV voltage | 9 | `0..30` volts | Power/all |
| PV current | 10 | `-0.5..+2.5` amperes | Power/all |
| IMU timeout button (`T`) | 11 | Pressed means no new IMU sample | IMU/all |
| Power fault button (`P`) | 12 | Pressed means the INA219 read failed | Power/all |

The knobs start near `0.5`, or about halfway through their range.

## Light-only test

1. Build with `-Mode light` and open `simulations/light_only/diagram.json`.
2. Keep all four light knobs around the middle while it sets neutral.
3. Look for `VALIDATION MODE: LIGHT ONLY` and a valid light reading.
4. Move one corner at a time. Pitch and roll error should change direction depending on which corner you move.
5. Move all four to `0.0`. This should give `TOO_DARK` (`0x40`).
6. Move all four to `1.0`. This should give `SATURATED | CHANNEL_SHORT` (`0x280`).
7. Put them back at `0.5` and check that the reading becomes valid again.

If one corner gets brighter, these are the signs to expect:

| Increased corner | Pitch error | Roll error |
| --- | ---: | ---: |
| Top-left | Positive | Positive |
| Top-right | Positive | Negative |
| Bottom-left | Negative | Positive |
| Bottom-right | Negative | Negative |

## IMU-only test

1. Build with `-Mode imu` and open `simulations/imu_only/diagram.json`.
2. Leave the pitch and roll knobs in the middle while it sets neutral.
3. Look for `VALIDATION MODE: IMU ONLY` and values close to `0,0`.
4. Move the knobs above and below the middle. The degree values should go positive and negative.
5. Hold `T` for more than 250 ms. You should see `TIMEOUT`, then `STALE_DATA`.
6. Let go of `T` and check that the reading becomes valid again.

This test starts with pretend pitch and roll angles. It does NOT recreate the BNO085's quaternion reports, I2C connection, timing, or accuracy.

## Power-only test

1. Build with `-Mode power` and open `simulations/power_only/diagram.json`.
2. Look for `VALIDATION MODE: POWER ONLY` and a valid reading.
3. Move the voltage and current knobs. The power value should be voltage multiplied by current.
4. Move voltage above about `0.867`. That puts it over 26 V and should give `OUT_OF_RANGE` (`0x1000`).
5. Move current below about `0.163`. That should give `UNEXPECTED_REVERSE_CURRENT` (`0x4000`).
6. Press `P` to cause `COMMUNICATION_ERROR`. Hold it until `STALE_DATA` also appears, then let go and check that the reading recovers.

This test uses pretend voltage and current values. It does NOT recreate the INA219's I2C connection, shunt accuracy, calibration, or wiring.

## Combined test

Build without a mode argument and open the root `diagram.json`. This runs all three sensor groups through one `sensor_adapter_read()` call. That combined function now calls the three individual public read APIs one after another, so the simulation exercises the same read paths Manuel can poll separately. I would recommend
changing one group at a time first so you can see what each one does. Then try changing multiple values or pressing the fault buttons together.

## Optional automated scenarios

If you want to run the automatic checks, these are the commands. Wokwi CLI needs a login and runs through Wokwi's online service:

```powershell
wokwi-cli . --timeout 90000 `
  --scenario scenarios/adapter-normal-and-faults.yaml

wokwi-cli simulations/light_only --timeout 90000 `
  --scenario light-only.yaml

wokwi-cli simulations/imu_only --timeout 90000 `
  --scenario imu-only.yaml

wokwi-cli simulations/power_only --timeout 90000 `
  --scenario power-only.yaml
```

If a test passes, it ends with `Scenario completed successfully`.

## Simulation files versus hardware files

`CONFIG_SOLARYOU_SIMULATION` picks the simulated or physical sensor code. A separate Kconfig choice picks all sensors or just one group.

Simulation backends:

- `simulation/imu_sensor_sim.cpp`
- `simulation/power_sensor_sim.cpp`
- production `light_sensor.cpp` reading controllable Wokwi ADC inputs

Physical backends:

- `sensor_bus.cpp`
- `imu_sensor.cpp`
- `power_sensor.cpp`
- production `light_sensor.cpp`

The modes share `sensor_api.cpp`, `sensor_math.cpp`, `sensor_status.cpp`, and `sensor_json.cpp` where they apply. None of these simulations test the final fully integrated code with RTOS tasks, queues, motors, networking, or dashboard.
