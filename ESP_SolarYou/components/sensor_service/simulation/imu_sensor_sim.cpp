#include "imu_sensor.h"

#include <Arduino.h>
#include <cmath>

#include "component_config.h"
#include "sensor_math.h"

static bool initialized = false;
static bool neutralValid = false;
static float neutralPitchDeg = 0.0f;
static float neutralRollDeg = 0.0f;
static sensor_health_t health;

static float adcToAngleDeg(uint16_t raw)
{
    const float normalized = static_cast<float>(raw) / 4095.0f;
    return ((normalized * 2.0f) - 1.0f) * IMU_SIM_MAX_ANGLE_DEG;
}

static bool readRaw(ImuRawReading &reading)
{
    if (!initialized) {
        return false;
    }

    if (digitalRead(IMU_SIM_TIMEOUT_GPIO) == LOW) {
        sensorHealthRaiseFaults(&health, SENSOR_FAULT_TIMEOUT);
        sensorHealthUpdateAge(&health, millis());
        if (health.has_sample && health.age_ms > IMU_READ_TIMEOUT_MS) {
            sensorHealthRaiseFaults(&health, SENSOR_FAULT_STALE_DATA);
        }
        sensorHealthSetState(&health, SENSOR_STATE_FAULT);
        return false;
    }

    reading.yaw_deg = 0.0f;
    reading.pitch_deg = adcToAngleDeg(
        static_cast<uint16_t>(analogRead(IMU_SIM_PITCH_GPIO)));
    reading.roll_deg = adcToAngleDeg(
        static_cast<uint16_t>(analogRead(IMU_SIM_ROLL_GPIO)));

    if (!std::isfinite(reading.pitch_deg) ||
        !std::isfinite(reading.roll_deg)) {
        sensorHealthRaiseFaults(&health, SENSOR_FAULT_NONFINITE_DATA);
        sensorHealthSetState(&health, SENSOR_STATE_FAULT);
        return false;
    }

    sensorHealthRecordSample(&health, millis());
    sensorHealthClearFaults(&health, SENSOR_FAULT_TIMEOUT |
                                     SENSOR_FAULT_STALE_DATA |
                                     SENSOR_FAULT_NONFINITE_DATA);
    return true;
}

bool imuSensorBegin()
{
    sensorHealthInitialize(&health, SENSOR_STATE_INITIALIZING);
    analogReadResolution(12);
    analogSetAttenuation(ADC_11db);
    pinMode(IMU_SIM_PITCH_GPIO, INPUT);
    pinMode(IMU_SIM_ROLL_GPIO, INPUT);
    pinMode(IMU_SIM_TIMEOUT_GPIO, INPUT_PULLUP);
    initialized = true;
    neutralValid = false;
    sensorHealthRaiseFaults(&health, SENSOR_FAULT_CALIBRATION_REQUIRED);
    sensorHealthSetState(&health, SENSOR_STATE_CALIBRATING);
    return true;
}

bool imuSensorCaptureNeutral(size_t sampleCount)
{
    if (!initialized || sampleCount == 0U) {
        sensorHealthRaiseFaults(&health, SENSOR_FAULT_CALIBRATION_REQUIRED);
        return false;
    }

    sensorHealthSetState(&health, SENSOR_STATE_CALIBRATING);
    float pitchSum = 0.0f;
    float rollSum = 0.0f;
    float pitchMin = INFINITY;
    float pitchMax = -INFINITY;
    float rollMin = INFINITY;
    float rollMax = -INFINITY;

    for (size_t index = 0; index < sampleCount; ++index) {
        ImuRawReading raw;
        if (!readRaw(raw)) {
            return false;
        }
        pitchSum += raw.pitch_deg;
        rollSum += raw.roll_deg;
        pitchMin = std::fmin(pitchMin, raw.pitch_deg);
        pitchMax = std::fmax(pitchMax, raw.pitch_deg);
        rollMin = std::fmin(rollMin, raw.roll_deg);
        rollMax = std::fmax(rollMax, raw.roll_deg);
        delay(5);
    }

    if ((pitchMax - pitchMin) > IMU_CAL_MAX_SPREAD_DEG ||
        (rollMax - rollMin) > IMU_CAL_MAX_SPREAD_DEG) {
        sensorHealthRaiseFaults(&health, SENSOR_FAULT_CALIBRATION_UNSTABLE);
        sensorHealthSetState(&health, SENSOR_STATE_FAULT);
        return false;
    }

    neutralPitchDeg = pitchSum / static_cast<float>(sampleCount);
    neutralRollDeg = rollSum / static_cast<float>(sampleCount);
    neutralValid = true;
    sensorHealthClearFaults(&health, SENSOR_FAULT_CALIBRATION_REQUIRED |
                                     SENSOR_FAULT_CALIBRATION_UNSTABLE);
    sensorHealthSetState(&health, SENSOR_STATE_VALID);
    return true;
}

bool imuSensorReadCentered(AxisReading &reading)
{
    reading = {0.0f, 0.0f, false};
    if (!neutralValid) {
        sensorHealthRaiseFaults(&health, SENSOR_FAULT_CALIBRATION_REQUIRED);
        sensorHealthSetState(&health, SENSOR_STATE_CALIBRATING);
        return false;
    }

    ImuRawReading raw;
    if (!readRaw(raw)) {
        return false;
    }

    reading.pitch = centerSensorValue(raw.pitch_deg, neutralPitchDeg,
        IMU_PITCH_SIGN, IMU_PITCH_DEADBAND_DEG, -180.0f, 180.0f);
    reading.roll = centerSensorValue(raw.roll_deg, neutralRollDeg,
        IMU_ROLL_SIGN, IMU_ROLL_DEADBAND_DEG, -180.0f, 180.0f);
    reading.valid = true;
    sensorHealthSetState(&health, SENSOR_STATE_VALID);
    return true;
}

void imuSensorGetHealth(sensor_health_t &output)
{
    sensorHealthUpdateAge(&health, millis());
    output = health;
}

void imuSensorClearLatchedFaults()
{
    sensorHealthClearLatched(&health);
}
