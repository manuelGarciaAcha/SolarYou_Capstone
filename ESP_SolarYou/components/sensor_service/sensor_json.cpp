#include "sensor_json.h"

#include <climits>
#include <cstring>

#include "cJSON.h"

static bool addNumber(cJSON *object, const char *key, double value)
{
    return cJSON_AddNumberToObject(object, key, value) != nullptr;
}

static bool addBoolean(cJSON *object, const char *key, bool value)
{
    return cJSON_AddBoolToObject(object, key, value) != nullptr;
}

static bool addNull(cJSON *object, const char *key)
{
    return cJSON_AddNullToObject(object, key) != nullptr;
}

static cJSON *addObject(cJSON *parent, const char *key)
{
    return cJSON_AddObjectToObject(parent, key);
}

static bool addHealth(cJSON *sensorObject, const sensor_health_t &health)
{
    cJSON *healthObject = addObject(sensorObject, SY_JSON_KEY_HEALTH);
    if (healthObject == nullptr) return false;

    return addNumber(healthObject, SY_JSON_KEY_STATE, health.state) &&
           addNumber(healthObject, SY_JSON_KEY_ACTIVE_FAULTS,
                     health.active_faults) &&
           addNumber(healthObject, SY_JSON_KEY_LATCHED_FAULTS,
                     health.latched_faults) &&
           addNumber(healthObject, SY_JSON_KEY_SAMPLE_TIME_MS,
                     health.sample_time_ms) &&
           addNumber(healthObject, SY_JSON_KEY_AGE_MS, health.age_ms) &&
           addNumber(healthObject, SY_JSON_KEY_SEQUENCE, health.sequence) &&
           addNumber(healthObject, SY_JSON_KEY_ERROR_COUNT,
                     health.error_count) &&
           addBoolean(healthObject, SY_JSON_KEY_HAS_SAMPLE, health.has_sample);
}

static bool addMeasurement(cJSON *object,
                           const char *key,
                           bool valid,
                           double value)
{
    return valid ? addNumber(object, key, value) : addNull(object, key);
}

static bool addRawAdc(cJSON *object,
                      const char *key,
                      bool hasSample,
                      uint16_t value)
{
    // Keeps the raw light numbers for troubleshooting even if the calculated result is invalid. Uses null if we have never gotten a sample.
    return hasSample ? addNumber(object, key, value) : addNull(object, key);
}

static bool populateRoot(cJSON *root, const sensor_snapshot_t &snapshot)
{
    if (!addNumber(root, SY_JSON_KEY_SCHEMA_VERSION,
                   SY_JSON_SCHEMA_VERSION) ||
        cJSON_AddStringToObject(root, SY_JSON_KEY_CONTRACT_STATUS,
                                SY_JSON_CONTRACT_STATUS) == nullptr ||
        !addNumber(root, SY_JSON_KEY_DEVICE_UPTIME_MS,
                   snapshot.device_uptime_ms) ||
        !addNumber(root, SY_JSON_KEY_SEQUENCE, snapshot.sequence) ||
        !addBoolean(root, SY_JSON_KEY_VALID, snapshot.valid)) {
        return false;
    }

    cJSON *imu = addObject(root, SY_JSON_KEY_IMU);
    if (imu == nullptr ||
        !addBoolean(imu, SY_JSON_KEY_VALID, snapshot.imu.valid) ||
        !addMeasurement(imu, SY_JSON_KEY_PITCH_DEG, snapshot.imu.valid,
                        snapshot.imu.pitch_deg) ||
        !addMeasurement(imu, SY_JSON_KEY_ROLL_DEG, snapshot.imu.valid,
                        snapshot.imu.roll_deg) ||
        !addHealth(imu, snapshot.imu.health)) {
        return false;
    }

    cJSON *light = addObject(root, SY_JSON_KEY_LIGHT);
    if (light == nullptr ||
        !addBoolean(light, SY_JSON_KEY_VALID, snapshot.light.valid) ||
        !addMeasurement(light, SY_JSON_KEY_PITCH_ERROR_NORM,
                        snapshot.light.valid, snapshot.light.pitch_error) ||
        !addMeasurement(light, SY_JSON_KEY_ROLL_ERROR_NORM,
                        snapshot.light.valid, snapshot.light.roll_error) ||
        !addRawAdc(light, SY_JSON_KEY_TOP_LEFT_ADC,
                   snapshot.light.health.has_sample,
                   snapshot.light.top_left_raw) ||
        !addRawAdc(light, SY_JSON_KEY_TOP_RIGHT_ADC,
                   snapshot.light.health.has_sample,
                   snapshot.light.top_right_raw) ||
        !addRawAdc(light, SY_JSON_KEY_BOTTOM_LEFT_ADC,
                   snapshot.light.health.has_sample,
                   snapshot.light.bottom_left_raw) ||
        !addRawAdc(light, SY_JSON_KEY_BOTTOM_RIGHT_ADC,
                   snapshot.light.health.has_sample,
                   snapshot.light.bottom_right_raw) ||
        !addHealth(light, snapshot.light.health)) {
        return false;
    }

    cJSON *power = addObject(root, SY_JSON_KEY_POWER);
    if (power == nullptr ||
        !addBoolean(power, SY_JSON_KEY_VALID, snapshot.power.valid) ||
        !addMeasurement(power, SY_JSON_KEY_LOAD_VOLTAGE_V,
                        snapshot.power.valid, snapshot.power.voltage_v) ||
        !addMeasurement(power, SY_JSON_KEY_CURRENT_A,
                        snapshot.power.valid, snapshot.power.current_a) ||
        !addMeasurement(power, SY_JSON_KEY_POWER_W,
                        snapshot.power.valid, snapshot.power.power_w) ||
        !addMeasurement(power, SY_JSON_KEY_SHUNT_VOLTAGE_MV,
                        snapshot.power.valid,
                        snapshot.power.shunt_voltage_mv) ||
        !addHealth(power, snapshot.power.health)) {
        return false;
    }

    return true;
}

sensor_json_result_t sensor_snapshot_to_json(const sensor_snapshot_t *snapshot,
                                             char *output,
                                             size_t outputCapacity,
                                             size_t *bytesWritten)
{
    if (bytesWritten != nullptr) *bytesWritten = 0U;
    if (output != nullptr && outputCapacity > 0U) output[0] = '\0';
    if (snapshot == nullptr || output == nullptr || outputCapacity == 0U ||
        outputCapacity > static_cast<size_t>(INT_MAX)) {
        return SENSOR_JSON_INVALID_ARGUMENT;
    }

    cJSON *root = cJSON_CreateObject();
    if (root == nullptr) return SENSOR_JSON_OUT_OF_MEMORY;
    if (!populateRoot(root, *snapshot)) {
        cJSON_Delete(root);
        return SENSOR_JSON_OUT_OF_MEMORY;
    }

    const cJSON_bool printed = cJSON_PrintPreallocated(
        root, output, static_cast<int>(outputCapacity), false);
    cJSON_Delete(root);
    if (!printed) {
        output[0] = '\0';
        return SENSOR_JSON_BUFFER_TOO_SMALL;
    }

    if (bytesWritten != nullptr) *bytesWritten = std::strlen(output);
    return SENSOR_JSON_OK;
}
