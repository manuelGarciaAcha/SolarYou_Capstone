# Individual sensor samples, combined snapshot, and draft JSON

The public C API now lets Manuel's control code poll the IMU, light array, and power sensor independently. Each call returns only that sensor's values, validity, and health information. The health information already includes that sensor's sample time, age, and sequence number.

The component still has a combined read for standalone validation, debugging, and the current draft JSON example. That combined function performs the three selected individual reads one after another. Manuel's higher-level integration will decide how to store and aggregate independently polled readings for control and telemetry.

The serializer only creates JSON text and nothing else. It does not connect to Wi-Fi, choose an endpoint, send a message, or create an RTOS task.

## Separate neutral functions

```c
bool sensor_adapter_capture_imu_neutral(void);
bool sensor_adapter_capture_light_neutral(void);
bool sensor_adapter_capture_neutral(void);
```

The IMU function saves the current stable physical orientation as the centered `0,0` reference. The light function saves the current evenly illuminated four-sensor balance as the zero-error reference. They are separate because an IMU reset should not replace the light reference. The original combined function stays available for standalone tests that intentionally capture both references together.

These functions save application references in RAM. They are not a replacement for the BNO085's internal sensor calibration, and the references are not currently saved across a full power cycle.

## Individual read functions

```c
bool sensor_adapter_read_imu(sensor_imu_sample_t *sample);
bool sensor_adapter_read_light(sensor_light_sample_t *sample);
bool sensor_adapter_read_power(sensor_power_sample_t *sample);
```

Each function performs one real read of only the named sensor group. A false return means that sample is not currently valid; the caller can still inspect the returned health information to determine why. The BNO085 and INA219 share the I2C bus, so the team must serialize those two calls if separate RTOS tasks could run them at the same time.

The combined convenience function is:

```c
bool sensor_adapter_read(sensor_snapshot_t *snapshot);
```

It is not required for Manuel's individual polling design. The current combined Wokwi test and JSON serializer keep using it so the full sensor path can still be exercised in one place.

## Current version

The C snapshot is version 1. The JSON message is still version 0 because we have to approve every JSON field name in next meeting maybe:

```json
"schema_version": 0,
"contract_status": "draft"
```

Version 0 is for development and will change after Manuel and Zack approve the names and behavior, once that's done the JSON contract can then move to version 1.

The complete example is in [`draft_sensor_message.json`](draft_sensor_message.json). Its values are examples only, they are not physical test results.

## Message layout

The root of the message contains:

| Field | Meaning |
| --- | --- |
| `schema_version` | Version of the JSON format |
| `contract_status` | Currently `draft` |
| `device_uptime_ms` | Time since ESP32 startup, not wall-clock time |
| `sequence` | Number of the complete snapshot |
| `valid` | True only when IMU, light, and power are all valid |
| `imu` | Orientation data and IMU health |
| `light` | Tracking errors, raw light readings, and light health |
| `power` | Voltage, current, calculated power, and power-sensor health |

### IMU fields

| Field | Meaning |
| --- | --- |
| `valid` | Whether the angle values can be used |
| `pitch_deg` | Neutral-centered pitch in degrees |
| `roll_deg` | Neutral-centered roll in degrees |
| `health` | State, faults, timing, and error information |

### Light fields

| Field | Meaning |
| --- | --- |
| `valid` | Whether the tracking errors can be used |
| `pitch_error_norm` | Normalized top-versus-bottom error |
| `roll_error_norm` | Normalized left-versus-right error |
| `top_left_adc` | Raw top-left ADC reading |
| `top_right_adc` | Raw top-right ADC reading |
| `bottom_left_adc` | Raw bottom-left ADC reading |
| `bottom_right_adc` | Raw bottom-right ADC reading |
| `health` | State, faults, timing, and error information |

The normalized light errors range from `-1.0` through `+1.0`. They are not angles in degrees. That requires a conversion

### Power fields

| Field | Meaning |
| --- | --- |
| `valid` | Whether the power values can be used |
| `load_voltage_v` | Draft name for the INA219 bus-voltage reading |
| `current_a` | Current in amperes |
| `power_w` | Voltage multiplied by current |
| `shunt_voltage_mv` | INA219 shunt voltage in millivolts |
| `health` | State, faults, timing, and error information |

I'm assuming that the INA219 is to go between the solar panel and the controller, but I need to confirm the wiring. Right now, load_voltage_v contains the INA219’s direct bus-voltage reading. That assumes the panel connects to VIN+ and the controller to VIN−, therefore that reading is on the controller side of the sensor. If the panel-side voltage is needed instead, I would add the shunt-voltage drop to the bus reading and update the power calculation and tests. The final field name can be choosen once the wiring and desired value are confirmed.

### Health fields

Each sensor has its own health object:

| Field | Meaning |
| --- | --- |
| `state` | Current sensor state number |
| `active_faults` | Faults happening now |
| `latched_faults` | Faults recorded since boot or the last clear |
| `sample_time_ms` | Uptime when the last good sample was recorded |
| `age_ms` | Age of the last good sample |
| `sequence` | Number of good samples recorded by that sensor |
| `error_count` | Number of recorded errors |
| `has_sample` | Whether the sensor has produced a good sample yet |

The state and fault values are listed in [`FAULT_CODES.md`](FAULT_CODES.md).

## Invalid data rules

- A real value of zero remains the number `0`.
- Negative angles and signed current remain negative numbers.
- Processed measurements become `null` when that sensor is invalid.
- Raw light values remain available when a raw sample exists, even if the tracking result is invalid.
- Raw light values become `null` when no sample has ever been received.
- Health information remains available even when measurements are invalid.

These rules prevent a missing measurement from looking like a real zero.

## JSON function

```c
sensor_json_result_t sensor_snapshot_to_json(
    const sensor_snapshot_t *snapshot,
    char *output,
    size_t output_capacity,
    size_t *bytes_written);
```

Give the JSON function a buffer of at least SENSOR_JSON_RECOMMENDED_CAPACITY bytes (it's currently 2048). That’s the space where it puts the message. If it works, it'll tell you how many bytes of JSON it wrote, the hidden end marker (NUL) is not included in that count. If it fails tho, it returns an error and says it wrote zero bytes. When JSON printing is turned on, the serial monitor puts sensor_json, before the message so we can spot it easily. That label is not actually part of the JSON—the actual message starts at {.

## Decisions still open

- Final spelling of the JSON field names
- Whether every health field is sent in normal dashboard messages
- Whether invalid measurements stay as `null` or are omitted
- Message rate and transport method
- Endpoint, port, authentication, and retry behavior
- Which application module owns transmission
- Whether raw readings are always sent or only sent in a debug mode

The current key strings are kept in `components/sensor_service/include/sensor_json_keys.h`, so they can be updated in one place after we all agree on the final contract.
