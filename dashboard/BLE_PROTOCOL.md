# BLE integration

The integrated firmware sends all eight dashboard fields, for example:

```json
{"pitch":1.25,"roll":-2.0,"power":1.235,"pitch_target":8.0,"roll_target":-4.0,"voltage":4.94,"current":0.25,"avg_power":1.125}
```

Angles are degrees, voltage is volts, current is amps, and power/average power
are watts. `avg_power` is the firmware's running average of valid power readings
since boot; the dashboard's displayed 60-second average is calculated separately.
All eight fields are present even when unavailable: invalid values are `null`,
and a real zero remains zero. Readings more than one second old are invalidated.
The existing INA219 reader supplies the electrical readings; BLE does not poll
the sensor separately. Angles use two decimal places and electrical readings
use three.
Full-control telemetry uses the existing pitch_light/roll_light controller
setpoints after a valid light update. Motor-off testing sends null targets.

The existing dashboard uses its zero/level reference when targets are unavailable.

## BLE identifiers

- Real-sensor device name: SolarYou-ASSTP
- Service: 47e8841f-04c9-4ae2-af68-a7b6f4eb293d
- Telemetry notification: f2d2680c-66bc-4e24-8eac-d8013a99320b
- Rate read/write: f2d2680c-66bc-4e24-8eac-d8013a99320c

The first three identifiers match Zack's parameters. The added rate characteristic
accepts {"set_rate_hz":5}, where the value is an
integer 1, 2, 5 or 10. Reading it returns the actual setting, e.g. {"rate_hz":5}.
Invalid writes leave the existing setting unchanged. Rate replies are control
information and never become sensor readings or CSV rows. Notification replies
are advisory; only reading the rate setting back can confirm the applied rate.

The initial connection check makes up to three read attempts, with a three-second
timeout per read and 250 ms pauses between failures. Telemetry notifications stay
active throughout. A missing-characteristic response means rate control is
unsupported and is not retried. Other failures show an error after three attempts;
the next Bluetooth connection starts a fresh check. These retries only read the
setting and do not resend rate-setting commands.

The selector immediately displays Applying. After the write, Verifying means
the dashboard is reading the applied setting back. Success is reported only
when that value matches the request. Error/disconnect cancels pending success.
The last confirmed choice is requested again after reconnect during the running
dashboard session. A failed restore keeps a healthy Bluetooth connection streaming,
shows the rate error, and allows a manual retry. If no rate has been confirmed on
this connection, the selector shows "Choose a rate" so the same choice can be
requested again directly. The prompt never sends a rate command.
A real disconnect still reconnects.
Missing rate support
leaves streaming available but disables the selector. The 450 ms minimum
visual feedback duration does not delay firmware operation or fake a reply.
The setting controls outgoing BLE updates, not IMU/PID sampling.

## Split messages

A complete UTF-8 JSON message is sent in one notification when it fits the
negotiated MTU minus three bytes. Otherwise notifications carry six-byte headers:

```text
1e | 01 | message sequence low | sequence high | index | count
```

This message sequence is necessary for reassembly and is not an extra reading
field. Indices start at zero. The backend joins matching ordered fragments
within two seconds and bounds assemblies at 4096 bytes and 32 pieces. Missing,
expired, out-of-order, or oversized assemblies are discarded; a new complete
message recovers. Disconnect clears unfinished pieces. Zack's original receiver
needs this matching backend addition to read split notifications.

See [setup](ESP32_INTEGRATION.md) for building and connecting the firmware.
