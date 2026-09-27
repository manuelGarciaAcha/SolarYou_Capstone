#include "sensor_api.h"

#include <Arduino.h>
#include <string.h>

#include "sdkconfig.h"
#include "imu_sensor.h"
#include "light_sensor.h"
#include "power_sensor.h"

static bool imuInitialized = false;
static bool imuCalibrated = false;
static bool lightInitialized = false;
static bool lightCalibrated = false;
static bool powerInitialized = false;
static uint32_t snapshotSequence = 0U;

bool sensor_adapter_init(void)
{
#if CONFIG_SOLARYOU_VALIDATION_ALL || CONFIG_SOLARYOU_VALIDATION_IMU_ONLY
    imuInitialized = imuSensorBegin();
    imuCalibrated = false;
#else
    imuInitialized = false;
    imuCalibrated = false;
#endif

#if CONFIG_SOLARYOU_VALIDATION_ALL || CONFIG_SOLARYOU_VALIDATION_LIGHT_ONLY
    lightSensorBegin();
    lightInitialized = true;
    lightCalibrated = false;
#else
    lightInitialized = false;
    lightCalibrated = false;
#endif

#if CONFIG_SOLARYOU_VALIDATION_ALL || CONFIG_SOLARYOU_VALIDATION_POWER_ONLY
    powerInitialized = powerSensorBegin();
#else
    powerInitialized = false;
#endif

    bool ready = true;
#if CONFIG_SOLARYOU_VALIDATION_ALL || CONFIG_SOLARYOU_VALIDATION_IMU_ONLY
    ready = ready && imuInitialized;
#endif
#if CONFIG_SOLARYOU_VALIDATION_ALL || CONFIG_SOLARYOU_VALIDATION_LIGHT_ONLY
    ready = ready && lightInitialized;
#endif
#if CONFIG_SOLARYOU_VALIDATION_ALL || CONFIG_SOLARYOU_VALIDATION_POWER_ONLY
    ready = ready && powerInitialized;
#endif
    return ready;
}

bool sensor_adapter_capture_imu_neutral(void)
{
#if CONFIG_SOLARYOU_VALIDATION_ALL || CONFIG_SOLARYOU_VALIDATION_IMU_ONLY
    imuCalibrated = imuInitialized &&
        imuSensorCaptureNeutral(IMU_NEUTRAL_SAMPLES);
    return imuCalibrated;
#else
    return false;
#endif
}

bool sensor_adapter_capture_light_neutral(void)
{
#if CONFIG_SOLARYOU_VALIDATION_ALL || CONFIG_SOLARYOU_VALIDATION_LIGHT_ONLY
    lightCalibrated = lightInitialized &&
        lightSensorCaptureNeutral(LIGHT_NEUTRAL_SAMPLES);
    return lightCalibrated;
#else
    return false;
#endif
}

bool sensor_adapter_capture_neutral(void)
{
    // This keeps the original all-in-one test path, but each reference can now
    // be captured separately when the main application needs it.
    bool ready = true;
#if CONFIG_SOLARYOU_VALIDATION_ALL || CONFIG_SOLARYOU_VALIDATION_IMU_ONLY
    const bool imuReady = sensor_adapter_capture_imu_neutral();
    ready = imuReady && ready;
#endif
#if CONFIG_SOLARYOU_VALIDATION_ALL || CONFIG_SOLARYOU_VALIDATION_LIGHT_ONLY
    const bool lightReady = sensor_adapter_capture_light_neutral();
    ready = lightReady && ready;
#endif
    // A power-only build has no neutral reference to capture, so it is ready.
    return ready;
}

bool sensor_adapter_read_imu(sensor_imu_sample_t *sample)
{
    if (sample == nullptr) return false;
    memset(sample, 0, sizeof(*sample));

#if CONFIG_SOLARYOU_VALIDATION_ALL || CONFIG_SOLARYOU_VALIDATION_IMU_ONLY
    AxisReading imu = {0.0f, 0.0f, false};
    if (imuInitialized && imuCalibrated) {
        imuSensorReadCentered(imu);
    }
    sample->valid = imu.valid;
    sample->pitch_deg = imu.pitch;
    sample->roll_deg = imu.roll;
    imuSensorGetHealth(sample->health);
    if ((sample->health.active_faults &
         SENSOR_FAULT_CALIBRATION_REQUIRED) != SENSOR_FAULT_NONE) {
        imuCalibrated = false;
    }
#endif
    return sample->valid;
}

bool sensor_adapter_read_light(sensor_light_sample_t *sample)
{
    if (sample == nullptr) return false;
    memset(sample, 0, sizeof(*sample));

#if CONFIG_SOLARYOU_VALIDATION_ALL || CONFIG_SOLARYOU_VALIDATION_LIGHT_ONLY
    LightReading light = {{0, 0, 0, 0}, 0.0f, 0.0f, false};
    if (lightInitialized && lightCalibrated) {
        lightSensorReadCentered(light);
    }
    sample->valid = light.valid;
    sample->pitch_error = light.pitch;
    sample->roll_error = light.roll;
    sample->top_left_raw = light.raw.topLeft;
    sample->top_right_raw = light.raw.topRight;
    sample->bottom_left_raw = light.raw.bottomLeft;
    sample->bottom_right_raw = light.raw.bottomRight;
    lightSensorGetHealth(sample->health);
    if ((sample->health.active_faults &
         SENSOR_FAULT_CALIBRATION_REQUIRED) != SENSOR_FAULT_NONE) {
        lightCalibrated = false;
    }
#endif
    return sample->valid;
}

bool sensor_adapter_read_power(sensor_power_sample_t *sample)
{
    if (sample == nullptr) return false;
    memset(sample, 0, sizeof(*sample));

#if CONFIG_SOLARYOU_VALIDATION_ALL || CONFIG_SOLARYOU_VALIDATION_POWER_ONLY
    PowerReading power = {0.0f, 0.0f, 0.0f, 0.0f, false};
    if (powerInitialized) {
        powerSensorRead(power);
    }
    sample->valid = power.valid;
    sample->voltage_v = power.busVoltageV;
    sample->current_a = power.currentA;
    sample->power_w = power.powerW;
    sample->shunt_voltage_mv = power.shuntVoltageMv;
    powerSensorGetHealth(sample->health);
#endif
    return sample->valid;
}

bool sensor_adapter_read(sensor_snapshot_t *snapshot)
{
    if (snapshot == nullptr) return false;
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->schema_version = SY_SENSOR_SCHEMA_VERSION;
    snapshot->device_uptime_ms = millis();
    snapshot->sequence = ++snapshotSequence;

    bool selectedSensorsValid = true;
    bool selectedAnySensor = false;
#if CONFIG_SOLARYOU_VALIDATION_ALL || CONFIG_SOLARYOU_VALIDATION_IMU_ONLY
    selectedAnySensor = true;
    const bool imuValid = sensor_adapter_read_imu(&snapshot->imu);
    selectedSensorsValid = imuValid && selectedSensorsValid;
#endif
#if CONFIG_SOLARYOU_VALIDATION_ALL || CONFIG_SOLARYOU_VALIDATION_LIGHT_ONLY
    selectedAnySensor = true;
    const bool lightValid = sensor_adapter_read_light(&snapshot->light);
    selectedSensorsValid = lightValid && selectedSensorsValid;
#endif
#if CONFIG_SOLARYOU_VALIDATION_ALL || CONFIG_SOLARYOU_VALIDATION_POWER_ONLY
    selectedAnySensor = true;
    const bool powerValid = sensor_adapter_read_power(&snapshot->power);
    selectedSensorsValid = powerValid && selectedSensorsValid;
#endif
    snapshot->valid = selectedAnySensor && selectedSensorsValid;
    return snapshot->valid;
}

void sensor_adapter_clear_latched_faults(void)
{
#if CONFIG_SOLARYOU_VALIDATION_ALL || CONFIG_SOLARYOU_VALIDATION_IMU_ONLY
    imuSensorClearLatchedFaults();
#endif
#if CONFIG_SOLARYOU_VALIDATION_ALL || CONFIG_SOLARYOU_VALIDATION_LIGHT_ONLY
    lightSensorClearLatchedFaults();
#endif
#if CONFIG_SOLARYOU_VALIDATION_ALL || CONFIG_SOLARYOU_VALIDATION_POWER_ONLY
    powerSensorClearLatchedFaults();
#endif
}
