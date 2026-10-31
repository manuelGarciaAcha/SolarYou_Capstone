# Data Schema (WORKING DRAFT)

Shared reference for the JSON fields passed between the ESP32 and the
dashboard. Please fill in or correct the real field name if it doesn't
match your firmware, and answer anything marked TBD.

## Already in the mock dashboard

These three are already wired up in app.py and dashboard.js, just running
on fake data for now:

| Field | Type | Notes |
|---|---|---|
| pitch | float, degrees | From IMU |
| roll | float, degrees | From IMU |
| power | float, watts | Calculated or direct reading? Still need to know |

## Still needed, not built yet

| What's needed | Proposed name | Type | Confirmed name | Owner | Notes |
|---|---|---|---|---|---|
| Target pitch | target_pitch | float, degrees | | M | For baseline/debug comparison |
| Target roll | target_roll | float, degrees | | M | Same |
| PID output | pid_command | float | | M | Useful for tuning, may need to be split roll/pitch |
| Motor roll angle | motor_roll_angle | float, degrees | | M | Output of command_motor_angle() |
| Motor pitch angle | motor_pitch_angle | float, degrees | | M | Same |
| Voltage | voltage | float, V | | J/M | See power note above |
| Current | current | float, A | | J/M | See power note above |
| Light sensors (x4) | light_sensors | array of 4 floats | | J/R | For tracking display |
| System mode | system_mode | string | | J | ACTIVE_STABILIZATION / PASSIVE_BASELINE / SAFE_STOP |
| Timestamp | timestamp | TBD | | whoever generates it | Need to know how is time tracked on-device? |

## Dashboard to ESP32 (commands, if we need this)

| What's needed | Proposed name | Type | Owner |
|---|---|---|---|
| Toggle tracking | tracking_enabled | bool | maps to g_trackingEnabled |
| Toggle ML feedforward | ml_feedforward_enabled | bool | maps to g_mlFeedforwardEnabled |

## Open questions

- Are we calculating power from voltage x current, or read directly off the sensor?
- One JSON message per update, or split into multiple message types?
- How often does a new message actually get sent?
- Is this going through the esp_http_server WebSocket setup?
