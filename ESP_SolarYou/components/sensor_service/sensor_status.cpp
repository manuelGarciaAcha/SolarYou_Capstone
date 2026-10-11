#include "sensor_status.h"

#include <limits.h>

void sensorHealthInitialize(sensor_health_t *health, sensor_state_t state)
{
    if (health == nullptr) {
        return;
    }
    health->state = state;
    health->active_faults = SENSOR_FAULT_NONE;
    health->latched_faults = SENSOR_FAULT_NONE;
    health->sample_time_ms = 0U;
    health->age_ms = UINT32_MAX;
    health->sequence = 0U;
    health->error_count = 0U;
    health->has_sample = false;
}

void sensorHealthSetState(sensor_health_t *health, sensor_state_t state)
{
    if (health != nullptr) {
        health->state = state;
    }
}

void sensorHealthSetActiveFaults(sensor_health_t *health,
                                 sensor_fault_flags_t faults)
{
    if (health == nullptr) {
        return;
    }
    const sensor_fault_flags_t newlyRaised = faults & ~health->active_faults;
    health->active_faults = faults;
    health->latched_faults |= faults;
    if (newlyRaised != SENSOR_FAULT_NONE) {
        ++health->error_count;
    }
}

void sensorHealthRaiseFaults(sensor_health_t *health,
                             sensor_fault_flags_t faults)
{
    if (health == nullptr || faults == SENSOR_FAULT_NONE) {
        return;
    }
    const sensor_fault_flags_t newlyRaised = faults & ~health->active_faults;
    health->active_faults |= faults;
    health->latched_faults |= faults;
    if (newlyRaised != SENSOR_FAULT_NONE) {
        ++health->error_count;
    }
}

void sensorHealthClearFaults(sensor_health_t *health,
                             sensor_fault_flags_t faults)
{
    if (health != nullptr) {
        health->active_faults &= ~faults;
    }
}

void sensorHealthRecordSample(sensor_health_t *health, uint32_t now_ms)
{
    if (health == nullptr) {
        return;
    }
    health->sample_time_ms = now_ms;
    health->age_ms = 0U;
    ++health->sequence;
    health->has_sample = true;
}

void sensorHealthUpdateAge(sensor_health_t *health, uint32_t now_ms)
{
    if (health == nullptr) {
        return;
    }
    health->age_ms = health->has_sample
        ? static_cast<uint32_t>(now_ms - health->sample_time_ms)
        : UINT32_MAX;
}

void sensorHealthClearLatched(sensor_health_t *health)
{
    if (health != nullptr) {
        health->latched_faults = SENSOR_FAULT_NONE;
    }
}

const char *sensorStateName(sensor_state_t state)
{
    switch (state) {
    case SENSOR_STATE_UNINITIALIZED: return "uninitialized";
    case SENSOR_STATE_INITIALIZING: return "initializing";
    case SENSOR_STATE_CALIBRATING: return "calibrating";
    case SENSOR_STATE_VALID: return "valid";
    case SENSOR_STATE_DEGRADED: return "degraded";
    case SENSOR_STATE_FAULT: return "fault";
    case SENSOR_STATE_RECOVERING: return "recovering";
    default: return "unknown";
    }
}

