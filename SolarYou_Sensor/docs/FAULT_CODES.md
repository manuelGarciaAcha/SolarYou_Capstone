# Sensor states and fault codes

This is a quick reference for the sensor status values used by the esp code and the draft dashboard message. The actual definitions are in
`components/sensor_service/include/sensor_status.h`.

Each sensor has one current state and two fault masks. More than one fault can be present at the same time.

## States

| Value | State | Meaning |
| ---: | --- | --- |
| 0 | `UNINITIALIZED` | The driver has not started. |
| 1 | `INITIALIZING` | The driver is starting the hardware. |
| 2 | `CALIBRATING` | The sensor needs a neutral reference or is collecting one. |
| 3 | `VALID` | A current usable sample is available. |
| 4 | `DEGRADED` | The sensor responded, but the conditions do not give a useful measurement. |
| 5 | `FAULT` | A hardware, communication, configuration, or range check failed. |
| 6 | `RECOVERING` | Reserved for a future retry/reconnect process. |

## Fault flags

| Hex | Decimal | Name | Meaning |
| ---: | ---: | --- | --- |
| `0x00000001` | 1 | `CALIBRATION_REQUIRED` | A neutral reference is missing or was invalidated. |
| `0x00000002` | 2 | `CALIBRATION_UNSTABLE` | The readings moved too much during calibration. |
| `0x00000004` | 4 | `COMMUNICATION_ERROR` | A sensor read or bus transaction failed. |
| `0x00000008` | 8 | `TIMEOUT` | A new sample did not arrive before the timeout. |
| `0x00000010` | 16 | `STALE_DATA` | The last good sample is too old. |
| `0x00000020` | 32 | `SENSOR_RESET` | The BNO085 reported a reset. |
| `0x00000040` | 64 | `TOO_DARK` | The total light level is too low for tracking. |
| `0x00000080` | 128 | `SATURATED` | At least one light input is near its maximum. |
| `0x00000100` | 256 | `CHANNEL_OPEN` | A light channel is near zero while the others have light. |
| `0x00000200` | 512 | `CHANNEL_SHORT` | A light channel is near full scale. |
| `0x00000400` | 1024 | `CHANNEL_STUCK` | Reserved; the detector is not implemented yet. |
| `0x00000800` | 2048 | `CHANNEL_MISMATCH` | Reserved for a possible future warning. |
| `0x00001000` | 4096 | `OUT_OF_RANGE` | A value is outside the configured limits. |
| `0x00002000` | 8192 | `NONFINITE_DATA` | A calculation produced NaN or infinity. |
| `0x00004000` | 16384 | `UNEXPECTED_REVERSE_CURRENT` | Current is flowing in the unexpected direction. |
| `0x00008000` | 32768 | `SUPPLY_UNSTABLE` | Reserved; the detector is not implemented yet. |
| `0x00010000` | 65536 | `CONFIGURATION_ERROR` | A required sensor setting could not be enabled. |

## Using the masks

`active_faults` contains problems happening now. `latched_faults` remembers problems that happened earlier, even after the active condition clears. The
latched history stays set until `sensor_adapter_clear_latched_faults()` is called.

Fault values can be combined. For example, decimal `320` (`0x140`) means:

```text
TOO_DARK | CHANNEL_OPEN
```

Control code should use a sensor only when:

- its `valid` flag is true;
- its state is `VALID`;
- `active_faults` is zero; and
- the sample age is within the limit agreed on by the team.

The dashboard may still display invalid or older diagnostic information, but that information should not be used to command movement.

A large difference between the four light sensors can be a real tracking direction, so it is currently not being treated as a fault until physical implementation shows that it is a fault. The calculated error is limited to the range `-1.0` through `+1.0` will have to apply a conversion to degrees, `CHANNEL_MISMATCH` remains reserved just in case the team decides to later add a warning that does not block tracking.

