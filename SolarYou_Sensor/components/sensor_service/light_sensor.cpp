#include "light_sensor.h"

#include <Arduino.h>
#include <cmath>
#include "component_config.h"
#include "sensor_math.h"

static bool initialized = false;
static bool neutralValid = false;
static float neutralPitch = 0.0f;
static float neutralRoll = 0.0f;
static sensor_health_t health;

static bool hasHardwareFault(sensor_fault_flags_t faults)
{
    const sensor_fault_flags_t hardwareFaults = SENSOR_FAULT_CHANNEL_OPEN |
        SENSOR_FAULT_CHANNEL_SHORT | SENSOR_FAULT_CHANNEL_STUCK;
    return (faults & hardwareFaults) != SENSOR_FAULT_NONE;
}

static bool readRaw(LightRawReading &reading)
{
    if (!initialized) return false;
    uint32_t tl = 0, tr = 0, bl = 0, br = 0;
    for (size_t i = 0; i < LIGHT_AVERAGE_SETS; ++i) {
        tl += analogRead(LIGHT_TOP_LEFT_GPIO);
        tr += analogRead(LIGHT_TOP_RIGHT_GPIO);
        bl += analogRead(LIGHT_BOTTOM_LEFT_GPIO);
        br += analogRead(LIGHT_BOTTOM_RIGHT_GPIO);
    }
    reading.topLeft = static_cast<uint16_t>(tl / LIGHT_AVERAGE_SETS);
    reading.topRight = static_cast<uint16_t>(tr / LIGHT_AVERAGE_SETS);
    reading.bottomLeft = static_cast<uint16_t>(bl / LIGHT_AVERAGE_SETS);
    reading.bottomRight = static_cast<uint16_t>(br / LIGHT_AVERAGE_SETS);
    sensorHealthRecordSample(&health, millis());
    return true;
}

void lightSensorBegin()
{
    sensorHealthInitialize(&health, SENSOR_STATE_INITIALIZING);
    analogReadResolution(12);
    // Set attenuation before the first ADC read. Arduino 3.x creates each
    // channel on that read, so setting it per pin here would be too early.
    analogSetAttenuation(ADC_11db);
    pinMode(LIGHT_TOP_LEFT_GPIO, INPUT);
    pinMode(LIGHT_TOP_RIGHT_GPIO, INPUT);
    pinMode(LIGHT_BOTTOM_LEFT_GPIO, INPUT);
    pinMode(LIGHT_BOTTOM_RIGHT_GPIO, INPUT);
    initialized = true;
    neutralValid = false;
    sensorHealthRaiseFaults(&health, SENSOR_FAULT_CALIBRATION_REQUIRED);
    sensorHealthSetState(&health, SENSOR_STATE_CALIBRATING);
}

bool lightSensorCaptureNeutral(size_t sampleCount)
{
    if (!initialized || sampleCount == 0U) {
        sensorHealthRaiseFaults(&health, SENSOR_FAULT_CALIBRATION_REQUIRED);
        return false;
    }
    sensorHealthSetState(&health, SENSOR_STATE_CALIBRATING);
    float pitchSum = 0.0f, rollSum = 0.0f;
    float pitchMin = INFINITY, pitchMax = -INFINITY;
    float rollMin = INFINITY, rollMax = -INFINITY;
    for (size_t i = 0; i < sampleCount; ++i) {
        LightRawReading raw;
        if (!readRaw(raw)) return false;
        float pitch = 0.0f, roll = 0.0f;
        const sensor_fault_flags_t faults = evaluateLightReading(
            raw, LIGHT_MIN_TOTAL_COUNT, LIGHT_SATURATION_COUNT,
            LIGHT_OPEN_COUNT, LIGHT_SHORT_COUNT, pitch, roll);
        if (faults != SENSOR_FAULT_NONE) {
            sensorHealthSetActiveFaults(
                &health, faults | SENSOR_FAULT_CALIBRATION_REQUIRED);
            return false;
        }
        if (std::fabs(pitch) > LIGHT_CAL_MAX_ABS_ERROR ||
            std::fabs(roll) > LIGHT_CAL_MAX_ABS_ERROR) {
            sensorHealthSetActiveFaults(
                &health, SENSOR_FAULT_CALIBRATION_REQUIRED);
            return false;
        }
        pitchSum += pitch;
        rollSum += roll;
        pitchMin = std::fmin(pitchMin, pitch);
        pitchMax = std::fmax(pitchMax, pitch);
        rollMin = std::fmin(rollMin, roll);
        rollMax = std::fmax(rollMax, roll);
        delay(10);
    }
    if ((pitchMax - pitchMin) > LIGHT_CAL_MAX_SPREAD ||
        (rollMax - rollMin) > LIGHT_CAL_MAX_SPREAD) {
        sensorHealthRaiseFaults(&health, SENSOR_FAULT_CALIBRATION_UNSTABLE);
        return false;
    }
    neutralPitch = pitchSum / sampleCount;
    neutralRoll = rollSum / sampleCount;
    neutralValid = true;
    sensorHealthSetActiveFaults(&health, SENSOR_FAULT_NONE);
    sensorHealthSetState(&health, SENSOR_STATE_VALID);
    return true;
}

bool lightSensorReadCentered(LightReading &reading)
{
    reading = {{0, 0, 0, 0}, 0.0f, 0.0f, false};
    if (!neutralValid || !readRaw(reading.raw)) {
        sensorHealthRaiseFaults(&health, SENSOR_FAULT_CALIBRATION_REQUIRED);
        sensorHealthSetState(&health, SENSOR_STATE_CALIBRATING);
        return false;
    }
    float pitch = 0.0f, roll = 0.0f;
    const sensor_fault_flags_t faults = evaluateLightReading(
        reading.raw, LIGHT_MIN_TOTAL_COUNT, LIGHT_SATURATION_COUNT,
        LIGHT_OPEN_COUNT, LIGHT_SHORT_COUNT, pitch, roll);
    sensorHealthSetActiveFaults(&health, faults);
    if (faults != SENSOR_FAULT_NONE) {
        sensorHealthSetState(&health, hasHardwareFault(faults)
            ? SENSOR_STATE_FAULT : SENSOR_STATE_DEGRADED);
        return true;
    }
    reading.pitch = centerSensorValue(pitch, neutralPitch, LIGHT_PITCH_SIGN,
                                      LIGHT_DEADBAND, -1.0f, 1.0f);
    reading.roll = centerSensorValue(roll, neutralRoll, LIGHT_ROLL_SIGN,
                                     LIGHT_DEADBAND, -1.0f, 1.0f);
    reading.valid = true;
    sensorHealthSetState(&health, SENSOR_STATE_VALID);
    return true;
}

void lightSensorGetHealth(sensor_health_t &output)
{
    sensorHealthUpdateAge(&health, millis());
    output = health;
}

void lightSensorClearLatchedFaults()
{
    sensorHealthClearLatched(&health);
}
