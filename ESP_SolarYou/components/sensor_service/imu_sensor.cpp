#include "imu_sensor.h"

#include <Arduino.h>
#include <Adafruit_BNO08x.h>
#include <Wire.h>
#include <cmath>

#include "component_config.h"
#include "sensor_bus.h"
#include "sensor_math.h"

static Adafruit_BNO08x bno085(-1);
static sh2_SensorValue_t sensorValue;
static bool initialized = false;
static bool neutralValid = false;
static float neutralPitchDeg = 0.0f;
static float neutralRollDeg = 0.0f;
static sensor_health_t health;

static bool quaternionToEuler(const sh2_RotationVectorWAcc_t &rotation,
                              ImuRawReading &reading)
{
    const float qr = rotation.real;
    const float qi = rotation.i;
    const float qj = rotation.j;
    const float qk = rotation.k;
    const float sqr = qr * qr;
    const float sqi = qi * qi;
    const float sqj = qj * qj;
    const float sqk = qk * qk;
    const float norm = sqr + sqi + sqj + sqk;
    if (norm <= 0.000001f) return false;

    reading.yaw_deg = std::atan2(2.0f * (qi * qj + qk * qr),
                                 sqi - sqj - sqk + sqr) * RAD_TO_DEG;
    float argument = -2.0f * (qi * qk - qj * qr) / norm;
    if (argument > 1.0f) argument = 1.0f;
    if (argument < -1.0f) argument = -1.0f;
    reading.pitch_deg = std::asin(argument) * RAD_TO_DEG;
    reading.roll_deg = std::atan2(2.0f * (qj * qk + qi * qr),
                                  -sqi - sqj + sqk + sqr) * RAD_TO_DEG;
    return std::isfinite(reading.yaw_deg) &&
           std::isfinite(reading.pitch_deg) &&
           std::isfinite(reading.roll_deg);
}

static bool readRaw(ImuRawReading &reading)
{
    if (!initialized) return false;
    const uint32_t start = millis();
    do {
        if (bno085.wasReset()) {
            neutralValid = false;
            sensorHealthRaiseFaults(&health, SENSOR_FAULT_SENSOR_RESET |
                                              SENSOR_FAULT_CALIBRATION_REQUIRED);
            sensorHealthSetState(&health, SENSOR_STATE_CALIBRATING);
            if (!bno085.enableReport(SH2_ROTATION_VECTOR,
                                     IMU_REPORT_INTERVAL_US)) {
                sensorHealthRaiseFaults(&health,
                                        SENSOR_FAULT_CONFIGURATION_ERROR);
            }
            // A reset wipes out the neutral reference. Don't treat this reset event like a normal calibrated reading.

            return false;
        }
        while (bno085.getSensorEvent(&sensorValue)) {
            if (sensorValue.sensorId == SH2_ROTATION_VECTOR &&
                quaternionToEuler(sensorValue.un.rotationVector, reading)) {
                sensorHealthRecordSample(&health, millis());
                sensorHealthClearFaults(&health, SENSOR_FAULT_COMMUNICATION_ERROR |
                                                  SENSOR_FAULT_TIMEOUT |
                                                  SENSOR_FAULT_STALE_DATA |
                                                  SENSOR_FAULT_NONFINITE_DATA);
                return true;
            }
        }
        delay(1);
    } while ((millis() - start) < IMU_READ_TIMEOUT_MS);

    sensorHealthRaiseFaults(&health, SENSOR_FAULT_TIMEOUT);
    sensorHealthUpdateAge(&health, millis());
    if (health.has_sample && health.age_ms > IMU_READ_TIMEOUT_MS) {
        sensorHealthRaiseFaults(&health, SENSOR_FAULT_STALE_DATA);
    }
    sensorHealthSetState(&health, SENSOR_STATE_FAULT);
    return false;
}

bool imuSensorBegin()
{
    sensorHealthInitialize(&health, SENSOR_STATE_INITIALIZING);
    if (!sensorBusBegin() || !bno085.begin_I2C(IMU_I2C_ADDRESS, &Wire)) {
        sensorHealthRaiseFaults(&health, SENSOR_FAULT_COMMUNICATION_ERROR);
        sensorHealthSetState(&health, SENSOR_STATE_FAULT);
        return false;
    }
    if (!bno085.enableReport(SH2_ROTATION_VECTOR, IMU_REPORT_INTERVAL_US)) {
        sensorHealthRaiseFaults(&health, SENSOR_FAULT_CONFIGURATION_ERROR);
        sensorHealthSetState(&health, SENSOR_STATE_FAULT);
        return false;
    }
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
    float pitchSum = 0.0f, rollSum = 0.0f;
    float pitchMin = INFINITY, pitchMax = -INFINITY;
    float rollMin = INFINITY, rollMax = -INFINITY;
    for (size_t i = 0; i < sampleCount; ++i) {
        ImuRawReading raw;
        if (!readRaw(raw)) return false;
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
        return false;
    }
    neutralPitchDeg = pitchSum / sampleCount;
    neutralRollDeg = rollSum / sampleCount;
    neutralValid = true;
    sensorHealthClearFaults(&health, SENSOR_FAULT_CALIBRATION_REQUIRED |
                                     SENSOR_FAULT_CALIBRATION_UNSTABLE |
                                     SENSOR_FAULT_SENSOR_RESET);
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
    if (!readRaw(raw)) return false;
    // The BNO can reset while we are reading it. If that happens this reading stays invalid until we capture neutral again.
    if (!neutralValid) return false;
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
