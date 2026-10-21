#include "power_sensor.h"

#include <Arduino.h>
#include <cmath>

#include "component_config.h"
#include "sensor_math.h"

static bool initialized = false;
static sensor_health_t health;

static float adcFraction(int gpio)
{
    return static_cast<float>(analogRead(gpio)) / 4095.0f;
}

bool powerSensorBegin()
{
    sensorHealthInitialize(&health, SENSOR_STATE_INITIALIZING);
    analogReadResolution(12);
    analogSetAttenuation(ADC_11db);
    pinMode(POWER_SIM_VOLTAGE_GPIO, INPUT);
    pinMode(POWER_SIM_CURRENT_GPIO, INPUT);
    pinMode(POWER_SIM_COMM_FAULT_GPIO, INPUT_PULLUP);
    initialized = true;
    sensorHealthSetState(&health, SENSOR_STATE_VALID);
    return true;
}

bool powerSensorRead(PowerReading &reading)
{
    reading = {0.0f, 0.0f, 0.0f, 0.0f, false};
    if (!initialized || digitalRead(POWER_SIM_COMM_FAULT_GPIO) == LOW) {
        sensor_fault_flags_t faults = SENSOR_FAULT_COMMUNICATION_ERROR;
        sensorHealthUpdateAge(&health, millis());
        if (health.has_sample && health.age_ms > POWER_STALE_TIMEOUT_MS) {
            faults |= SENSOR_FAULT_STALE_DATA;
        }
        sensorHealthSetActiveFaults(&health, faults);
        sensorHealthSetState(&health, SENSOR_STATE_FAULT);
        return false;
    }

    const float voltageFraction = adcFraction(POWER_SIM_VOLTAGE_GPIO);
    const float currentFraction = adcFraction(POWER_SIM_CURRENT_GPIO);
    reading.busVoltageV = voltageFraction * POWER_SIM_MAX_VOLTAGE_V;
    reading.currentA = POWER_SIM_MIN_CURRENT_A +
        currentFraction * (POWER_SIM_MAX_CURRENT_A -
                           POWER_SIM_MIN_CURRENT_A);
    reading.shuntVoltageMv = reading.currentA * POWER_SIM_SHUNT_OHMS *
                             1000.0f;

    sensor_fault_flags_t faults = SENSOR_FAULT_NONE;
    if (!std::isfinite(reading.busVoltageV) ||
        !std::isfinite(reading.shuntVoltageMv) ||
        !std::isfinite(reading.currentA)) {
        faults |= SENSOR_FAULT_NONFINITE_DATA;
    }
    if (std::isfinite(reading.busVoltageV) &&
        (reading.busVoltageV < 0.0f ||
         reading.busVoltageV > POWER_MAX_BUS_VOLTAGE_V)) {
        faults |= SENSOR_FAULT_OUT_OF_RANGE;
    }
    if (std::isfinite(reading.currentA) &&
        std::fabs(reading.currentA) > POWER_MAX_CURRENT_A) {
        faults |= SENSOR_FAULT_OUT_OF_RANGE;
    }
    if (std::isfinite(reading.currentA) &&
        reading.currentA < POWER_REVERSE_FAULT_A) {
        faults |= SENSOR_FAULT_UNEXPECTED_REVERSE_CURRENT;
    }
    if (!calculateElectricalPower(reading.busVoltageV, reading.currentA,
                                  reading.powerW)) {
        faults |= SENSOR_FAULT_NONFINITE_DATA;
    }

    const uint32_t now = millis();
    if (faults == SENSOR_FAULT_NONE) {
        sensorHealthRecordSample(&health, now);
    } else {
        sensorHealthUpdateAge(&health, now);
        if (health.has_sample && health.age_ms > POWER_STALE_TIMEOUT_MS) {
            faults |= SENSOR_FAULT_STALE_DATA;
        }
    }
    sensorHealthSetActiveFaults(&health, faults);
    sensorHealthSetState(&health, faults == SENSOR_FAULT_NONE
        ? SENSOR_STATE_VALID : SENSOR_STATE_FAULT);
    reading.valid = faults == SENSOR_FAULT_NONE;
    return reading.valid;
}

void powerSensorGetHealth(sensor_health_t &output)
{
    sensorHealthUpdateAge(&health, millis());
    output = health;
}

void powerSensorClearLatchedFaults()
{
    sensorHealthClearLatched(&health);
}
