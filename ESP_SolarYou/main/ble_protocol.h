#ifndef SOLARYOU_BLE_PROTOCOL_H
#define SOLARYOU_BLE_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SY_BLE_DEVICE_NAME "SolarYou-ASSTP"
#define SY_BLE_SERVICE_UUID "47e8841f-04c9-4ae2-af68-a7b6f4eb293d"
#define SY_BLE_TELEMETRY_UUID "f2d2680c-66bc-4e24-8eac-d8013a99320b"
#define SY_BLE_RATE_UUID "f2d2680c-66bc-4e24-8eac-d8013a99320c"
#define SY_BLE_JSON_CAPACITY 256U
#define SY_BLE_FRAGMENT_HEADER 6U
#define SY_BLE_FRAGMENT_MAGIC 0x1eU
#define SY_BLE_FRAGMENT_VERSION 1U

typedef struct {
    float pitch, roll, power, pitch_target, roll_target;
    float voltage, current, avg_power;
    bool imu_valid, power_valid, targets_valid;
    uint32_t captured_ms; /* Internal freshness timestamp; never sent on wire. */
} sy_telemetry_sample_t;

/* Copy real controller setpoints only when the caller has valid targets.
 * Zero is a valid target; unavailable/nonfinite pairs remain null on the wire. */
void sy_telemetry_set_targets(sy_telemetry_sample_t *sample, float pitch,
                              float roll, bool available);
/* Returns bytes written, or zero on truncation/invalid arguments. */
size_t sy_telemetry_format(char *out, size_t capacity, const sy_telemetry_sample_t *sample);
bool sy_ble_rate_allowed(unsigned rate_hz);
bool sy_ble_parse_rate_command(const uint8_t *data, size_t length, unsigned *rate_hz);
size_t sy_ble_rate_reply(char *out, size_t capacity, unsigned rate_hz);
size_t sy_ble_fragment_count(size_t json_length, size_t payload_limit);
size_t sy_ble_fragment(const uint8_t *json, size_t json_length, uint16_t sequence,
                       size_t payload_limit, size_t index, uint8_t *out, size_t capacity);

/* Single-owner model. The ESP adapter protects calls with a short critical section. */
typedef struct {
    bool connected, subscribed;
    uint16_t mtu;
    unsigned rate_hz;
    uint32_t generation, next_due_ms;
} sy_ble_session_t;
void sy_ble_session_init(sy_ble_session_t *session);
void sy_ble_session_connect(sy_ble_session_t *session);
void sy_ble_session_disconnect(sy_ble_session_t *session);
void sy_ble_session_subscribe(sy_ble_session_t *session, bool enabled, uint32_t now_ms);
bool sy_ble_session_set_rate(sy_ble_session_t *session, unsigned hz, uint32_t now_ms);
bool sy_ble_session_due(sy_ble_session_t *session, uint32_t now_ms);

#ifdef __cplusplus
}
#endif
#endif
