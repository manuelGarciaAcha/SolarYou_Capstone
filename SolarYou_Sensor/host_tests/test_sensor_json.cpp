#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>

#include "cJSON.h"
#include "sensor_json.h"

static cJSON *requiredObject(cJSON *parent, const char *key)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(parent, key);
    assert(cJSON_IsObject(item));
    return item;
}

static cJSON *requiredItem(cJSON *parent, const char *key)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(parent, key);
    assert(item != nullptr);
    return item;
}

static void assertNumber(cJSON *parent,
                         const char *key,
                         double expected,
                         double tolerance = 0.0001)
{
    cJSON *item = requiredItem(parent, key);
    assert(cJSON_IsNumber(item));
    assert(std::fabs(item->valuedouble - expected) <= tolerance);
}

static sensor_health_t makeHealth(sensor_state_t state,
                                  sensor_fault_flags_t activeFaults,
                                  bool hasSample,
                                  uint32_t sequence)
{
    sensor_health_t health = {};
    health.state = state;
    health.active_faults = activeFaults;
    health.latched_faults = activeFaults | SENSOR_FAULT_SENSOR_RESET;
    health.sample_time_ms = 123400U;
    health.age_ms = 56U;
    health.sequence = sequence;
    health.error_count = 2U;
    health.has_sample = hasSample;
    return health;
}

static sensor_snapshot_t makeValidSnapshot()
{
    sensor_snapshot_t snapshot = {};
    snapshot.schema_version = SY_SENSOR_SCHEMA_VERSION;
    snapshot.device_uptime_ms = 123456U;
    snapshot.sequence = 42U;
    snapshot.valid = true;

    snapshot.imu.valid = true;
    snapshot.imu.pitch_deg = -2.35f;
    snapshot.imu.roll_deg = 0.0f;
    snapshot.imu.health = makeHealth(SENSOR_STATE_VALID, SENSOR_FAULT_NONE,
                                     true, 416U);

    snapshot.light.valid = true;
    snapshot.light.pitch_error = 0.1042f;
    snapshot.light.roll_error = -0.0317f;
    snapshot.light.top_left_raw = 1820U;
    snapshot.light.top_right_raw = 1760U;
    snapshot.light.bottom_left_raw = 1510U;
    snapshot.light.bottom_right_raw = 1580U;
    snapshot.light.health = makeHealth(SENSOR_STATE_VALID, SENSOR_FAULT_NONE,
                                       true, 466U);

    snapshot.power.valid = true;
    snapshot.power.voltage_v = 12.48f;
    snapshot.power.current_a = -0.73f;
    snapshot.power.power_w = -9.1104f;
    snapshot.power.shunt_voltage_mv = 0.0f;
    snapshot.power.health = makeHealth(SENSOR_STATE_VALID, SENSOR_FAULT_NONE,
                                       true, 417U);
    return snapshot;
}

static cJSON *serializeAndParse(const sensor_snapshot_t &snapshot,
                                char *output,
                                size_t capacity)
{
    size_t bytesWritten = 999U;
    assert(sensor_snapshot_to_json(&snapshot, output, capacity,
                                   &bytesWritten) == SENSOR_JSON_OK);
    assert(bytesWritten == std::strlen(output));
    assert(bytesWritten > 0U);
    cJSON *root = cJSON_Parse(output);
    assert(root != nullptr);
    return root;
}

static void testValidNestedSnapshot()
{
    const sensor_snapshot_t snapshot = makeValidSnapshot();
    char output[SENSOR_JSON_RECOMMENDED_CAPACITY];
    cJSON *root = serializeAndParse(snapshot, output, sizeof(output));

    assertNumber(root, SY_JSON_KEY_SCHEMA_VERSION, SY_JSON_SCHEMA_VERSION);
    cJSON *status = requiredItem(root, SY_JSON_KEY_CONTRACT_STATUS);
    assert(cJSON_IsString(status));
    assert(std::strcmp(status->valuestring, SY_JSON_CONTRACT_STATUS) == 0);
    assertNumber(root, SY_JSON_KEY_DEVICE_UPTIME_MS, 123456.0);
    assertNumber(root, SY_JSON_KEY_SEQUENCE, 42.0);
    assert(cJSON_IsTrue(requiredItem(root, SY_JSON_KEY_VALID)));

    cJSON *imu = requiredObject(root, SY_JSON_KEY_IMU);
    assert(cJSON_IsTrue(requiredItem(imu, SY_JSON_KEY_VALID)));
    assertNumber(imu, SY_JSON_KEY_PITCH_DEG, -2.35);
    // A real zero should stay a number in JSON, not turn into null.
    assertNumber(imu, SY_JSON_KEY_ROLL_DEG, 0.0);

    cJSON *light = requiredObject(root, SY_JSON_KEY_LIGHT);
    assertNumber(light, SY_JSON_KEY_PITCH_ERROR_NORM, 0.1042);
    assertNumber(light, SY_JSON_KEY_ROLL_ERROR_NORM, -0.0317);
    assertNumber(light, SY_JSON_KEY_TOP_LEFT_ADC, 1820.0);

    cJSON *power = requiredObject(root, SY_JSON_KEY_POWER);
    assertNumber(power, SY_JSON_KEY_LOAD_VOLTAGE_V, 12.48);
    assertNumber(power, SY_JSON_KEY_CURRENT_A, -0.73);
    assertNumber(power, SY_JSON_KEY_POWER_W, -9.1104);
    assertNumber(power, SY_JSON_KEY_SHUNT_VOLTAGE_MV, 0.0);

    cJSON *powerHealth = requiredObject(power, SY_JSON_KEY_HEALTH);
    assertNumber(powerHealth, SY_JSON_KEY_SEQUENCE, 417.0);
    assert(cJSON_IsTrue(requiredItem(powerHealth, SY_JSON_KEY_HAS_SAMPLE)));
    cJSON_Delete(root);
}

static void testInvalidValuesAndFaultMasks()
{
    sensor_snapshot_t snapshot = makeValidSnapshot();
    snapshot.valid = false;
    snapshot.imu.valid = false;
    snapshot.imu.health = makeHealth(
        SENSOR_STATE_FAULT,
        SENSOR_FAULT_COMMUNICATION_ERROR | SENSOR_FAULT_TIMEOUT,
        false,
        std::numeric_limits<uint32_t>::max());
    snapshot.light.valid = false;
    snapshot.light.health = makeHealth(SENSOR_STATE_DEGRADED,
                                       SENSOR_FAULT_TOO_DARK,
                                       true, 9U);
    snapshot.power.valid = false;
    snapshot.power.health = makeHealth(SENSOR_STATE_FAULT,
                                       SENSOR_FAULT_OUT_OF_RANGE,
                                       true, 10U);
    snapshot.device_uptime_ms = std::numeric_limits<uint32_t>::max();
    snapshot.sequence = std::numeric_limits<uint32_t>::max();

    char output[SENSOR_JSON_RECOMMENDED_CAPACITY];
    cJSON *root = serializeAndParse(snapshot, output, sizeof(output));
    assert(cJSON_IsFalse(requiredItem(root, SY_JSON_KEY_VALID)));
    assertNumber(root, SY_JSON_KEY_DEVICE_UPTIME_MS, 4294967295.0, 0.0);
    assertNumber(root, SY_JSON_KEY_SEQUENCE, 4294967295.0, 0.0);

    cJSON *imu = requiredObject(root, SY_JSON_KEY_IMU);
    assert(cJSON_IsNull(requiredItem(imu, SY_JSON_KEY_PITCH_DEG)));
    assert(cJSON_IsNull(requiredItem(imu, SY_JSON_KEY_ROLL_DEG)));
    cJSON *imuHealth = requiredObject(imu, SY_JSON_KEY_HEALTH);
    assertNumber(imuHealth, SY_JSON_KEY_ACTIVE_FAULTS,
                 SENSOR_FAULT_COMMUNICATION_ERROR | SENSOR_FAULT_TIMEOUT);
    assertNumber(imuHealth, SY_JSON_KEY_SEQUENCE, 4294967295.0, 0.0);

    cJSON *light = requiredObject(root, SY_JSON_KEY_LIGHT);
    assert(cJSON_IsNull(requiredItem(light, SY_JSON_KEY_PITCH_ERROR_NORM)));
    // The light result can be invalid while the raw readings are still useful.
    assertNumber(light, SY_JSON_KEY_TOP_LEFT_ADC, 1820.0);

    cJSON *power = requiredObject(root, SY_JSON_KEY_POWER);
    assert(cJSON_IsNull(requiredItem(power, SY_JSON_KEY_LOAD_VOLTAGE_V)));
    assert(cJSON_IsNull(requiredItem(power, SY_JSON_KEY_CURRENT_A)));
    assert(cJSON_IsNull(requiredItem(power, SY_JSON_KEY_POWER_W)));
    assert(cJSON_IsNull(requiredItem(power, SY_JSON_KEY_SHUNT_VOLTAGE_MV)));
    cJSON_Delete(root);

    // If we never got a light sample, even the raw fields should be null.
    snapshot.light.health.has_sample = false;
    root = serializeAndParse(snapshot, output, sizeof(output));
    light = requiredObject(root, SY_JSON_KEY_LIGHT);
    assert(cJSON_IsNull(requiredItem(light, SY_JSON_KEY_TOP_LEFT_ADC)));
    assert(cJSON_IsNull(requiredItem(light, SY_JSON_KEY_BOTTOM_RIGHT_ADC)));
    cJSON_Delete(root);
}

static void testErrorResults()
{
    const sensor_snapshot_t snapshot = makeValidSnapshot();
    char output[16] = "not-empty";
    size_t bytesWritten = 7U;
    assert(sensor_snapshot_to_json(&snapshot, output, sizeof(output),
                                   &bytesWritten) ==
           SENSOR_JSON_BUFFER_TOO_SMALL);
    assert(output[0] == '\0');
    assert(bytesWritten == 0U);

    assert(sensor_snapshot_to_json(nullptr, output, sizeof(output),
                                   &bytesWritten) ==
           SENSOR_JSON_INVALID_ARGUMENT);
    assert(sensor_snapshot_to_json(&snapshot, nullptr, sizeof(output),
                                   &bytesWritten) ==
           SENSOR_JSON_INVALID_ARGUMENT);
    assert(sensor_snapshot_to_json(&snapshot, output, 0U,
                                   &bytesWritten) ==
           SENSOR_JSON_INVALID_ARGUMENT);
}

int main()
{
    testValidNestedSnapshot();
    testInvalidValuesAndFaultMasks();
    testErrorResults();
    std::cout << "PASS: draft nested sensor JSON semantics and buffer handling"
              << std::endl;
    return 0;
}
