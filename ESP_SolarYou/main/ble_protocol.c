#include "ble_protocol.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static void number(char *out, size_t capacity, float value, bool valid, int decimals)
{
    /* Bound the representation as well as excluding nan/inf. This limit is far
       outside the physical sensor range, and prevents a corrupt float from
       expanding a bounded message into hundreds of characters. */
    if (valid && isfinite(value) && fabsf(value) <= 1000000.0f) {
        (void)snprintf(out, capacity, "%.*f", decimals, (double)value);
    } else {
        (void)snprintf(out, capacity, "null");
    }
}

void sy_telemetry_set_targets(sy_telemetry_sample_t *sample, float pitch,
                              float roll, bool available)
{
    if (sample == NULL) return;
    sample->pitch_target = pitch;
    sample->roll_target = roll;
    sample->targets_valid = available && isfinite(pitch) && isfinite(roll) &&
        fabsf(pitch) <= 1000000.0f && fabsf(roll) <= 1000000.0f;
}

size_t sy_telemetry_format(char *out, size_t capacity, const sy_telemetry_sample_t *s)
{
    if (out == NULL || capacity == 0U || s == NULL) return 0U;
    char pitch[24], roll[24], power[24], pt[24], rt[24];
    char voltage[24], current[24], avg_power[24];
    number(pitch, sizeof(pitch), s->pitch, s->imu_valid, 2);
    number(roll, sizeof(roll), s->roll, s->imu_valid, 2);
    number(power, sizeof(power), s->power, s->power_valid, 3);
    number(pt, sizeof(pt), s->pitch_target, s->targets_valid, 2);
    number(rt, sizeof(rt), s->roll_target, s->targets_valid, 2);
    number(voltage, sizeof(voltage), s->voltage, s->power_valid, 3);
    number(current, sizeof(current), s->current, s->power_valid, 3);
    number(avg_power, sizeof(avg_power), s->avg_power, s->power_valid, 3);
    int count = snprintf(out, capacity,
        "{\"pitch\":%s,\"roll\":%s,\"power\":%s,\"pitch_target\":%s,\"roll_target\":%s,"
        "\"voltage\":%s,\"current\":%s,\"avg_power\":%s}",
        pitch, roll, power, pt, rt, voltage, current, avg_power);
    if (count < 0 || (size_t)count >= capacity) { out[0] = '\0'; return 0U; }
    return (size_t)count;
}

bool sy_ble_rate_allowed(unsigned hz)
{
    return hz == 1U || hz == 2U || hz == 5U || hz == 10U;
}

static void skip_space(const uint8_t *data, size_t length, size_t *i)
{
    while (*i < length && (data[*i] == ' ' || data[*i] == '\r' ||
           data[*i] == '\n' || data[*i] == '\t')) ++*i;
}

bool sy_ble_parse_rate_command(const uint8_t *data, size_t length, unsigned *hz)
{
    if (data == NULL || hz == NULL || length == 0U || length > 32U) return false;
    size_t i = 0U;
    skip_space(data, length, &i);
    if (i >= length || data[i++] != '{') return false;
    skip_space(data, length, &i);
    static const char key[] = "\"set_rate_hz\"";
    size_t key_len = sizeof(key) - 1U;
    if (length - i < key_len || memcmp(data + i, key, key_len) != 0) return false;
    i += key_len;
    skip_space(data, length, &i);
    if (i >= length || data[i++] != ':') return false;
    skip_space(data, length, &i);
    if (i >= length || data[i] < '1' || data[i] > '9') return false;
    unsigned value = (unsigned)(data[i++] - '0');
    if (i < length && data[i] >= '0' && data[i] <= '9') value = value * 10U + (unsigned)(data[i++] - '0');
    skip_space(data, length, &i);
    if (i >= length || data[i++] != '}') return false;
    skip_space(data, length, &i);
    if (i != length || !sy_ble_rate_allowed(value)) return false;
    *hz = value;
    return true;
}

size_t sy_ble_rate_reply(char *out, size_t capacity, unsigned hz)
{
    if (out == NULL || capacity == 0U || !sy_ble_rate_allowed(hz)) return 0U;
    int n = snprintf(out, capacity, "{\"rate_hz\":%u}", hz);
    if (n < 0 || (size_t)n >= capacity) { out[0] = '\0'; return 0U; }
    return (size_t)n;
}

size_t sy_ble_fragment_count(size_t length, size_t limit)
{
    if (length == 0U || length >= SY_BLE_JSON_CAPACITY || limit == 0U) return 0U;
    if (length <= limit) return 1U;
    if (limit <= SY_BLE_FRAGMENT_HEADER) return 0U;
    size_t count = (length + limit - SY_BLE_FRAGMENT_HEADER - 1U) / (limit - SY_BLE_FRAGMENT_HEADER);
    return count <= 255U ? count : 0U;
}

size_t sy_ble_fragment(const uint8_t *json, size_t length, uint16_t sequence,
                       size_t limit, size_t index, uint8_t *out, size_t capacity)
{
    size_t count = sy_ble_fragment_count(length, limit);
    if (json == NULL || out == NULL || count == 0U || index >= count) return 0U;
    if (count == 1U) {
        if (capacity < length) return 0U;
        memcpy(out, json, length);
        return length;
    }
    size_t chunk_capacity = limit - SY_BLE_FRAGMENT_HEADER;
    size_t offset = index * chunk_capacity;
    size_t chunk = length - offset;
    if (chunk > chunk_capacity) chunk = chunk_capacity;
    if (capacity < chunk + SY_BLE_FRAGMENT_HEADER) return 0U;
    out[0] = SY_BLE_FRAGMENT_MAGIC;
    out[1] = SY_BLE_FRAGMENT_VERSION;
    out[2] = (uint8_t)(sequence & 0xffU);
    out[3] = (uint8_t)(sequence >> 8U);
    out[4] = (uint8_t)index;
    out[5] = (uint8_t)count;
    memcpy(out + SY_BLE_FRAGMENT_HEADER, json + offset, chunk);
    return chunk + SY_BLE_FRAGMENT_HEADER;
}

void sy_ble_session_init(sy_ble_session_t *s)
{
    memset(s, 0, sizeof(*s)); s->mtu = 23U; s->rate_hz = 5U;
}
void sy_ble_session_connect(sy_ble_session_t *s)
{
    s->connected = true; s->subscribed = false; s->mtu = 23U; ++s->generation;
}
void sy_ble_session_disconnect(sy_ble_session_t *s)
{
    s->connected = false; s->subscribed = false; s->mtu = 23U; ++s->generation;
}
void sy_ble_session_subscribe(sy_ble_session_t *s, bool enabled, uint32_t now)
{
    s->subscribed = s->connected && enabled; s->next_due_ms = now;
}
bool sy_ble_session_set_rate(sy_ble_session_t *s, unsigned hz, uint32_t now)
{
    if (!sy_ble_rate_allowed(hz)) return false;
    s->rate_hz = hz; s->next_due_ms = now; return true;
}
bool sy_ble_session_due(sy_ble_session_t *s, uint32_t now)
{
    if (!s->connected || !s->subscribed || (int32_t)(now - s->next_due_ms) < 0) return false;
    s->next_due_ms = now + 1000U / s->rate_hz;
    return true;
}
