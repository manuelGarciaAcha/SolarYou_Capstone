#ifndef SENSOR_STATUS_H
#define SENSOR_STATUS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SENSOR_STATE_UNINITIALIZED = 0,
    SENSOR_STATE_INITIALIZING = 1,
    SENSOR_STATE_CALIBRATING = 2,
    SENSOR_STATE_VALID = 3,
    SENSOR_STATE_DEGRADED = 4,
    SENSOR_STATE_FAULT = 5,
    SENSOR_STATE_RECOVERING = 6
} sensor_state_t;

typedef uint32_t sensor_fault_flags_t;

#define SENSOR_FAULT_NONE                       UINT32_C(0x00000000)
#define SENSOR_FAULT_CALIBRATION_REQUIRED       UINT32_C(0x00000001)
#define SENSOR_FAULT_CALIBRATION_UNSTABLE       UINT32_C(0x00000002)
#define SENSOR_FAULT_COMMUNICATION_ERROR        UINT32_C(0x00000004)
#define SENSOR_FAULT_TIMEOUT                    UINT32_C(0x00000008)
#define SENSOR_FAULT_STALE_DATA                 UINT32_C(0x00000010)
#define SENSOR_FAULT_SENSOR_RESET               UINT32_C(0x00000020)
#define SENSOR_FAULT_TOO_DARK                   UINT32_C(0x00000040)
#define SENSOR_FAULT_SATURATED                  UINT32_C(0x00000080)
#define SENSOR_FAULT_CHANNEL_OPEN               UINT32_C(0x00000100)
#define SENSOR_FAULT_CHANNEL_SHORT              UINT32_C(0x00000200)
#define SENSOR_FAULT_CHANNEL_STUCK              UINT32_C(0x00000400)
/* Saved for a future warning so that it won't block readings, it's not used yet. */
#define SENSOR_FAULT_CHANNEL_MISMATCH            UINT32_C(0x00000800)
#define SENSOR_FAULT_OUT_OF_RANGE               UINT32_C(0x00001000)
#define SENSOR_FAULT_NONFINITE_DATA             UINT32_C(0x00002000)
#define SENSOR_FAULT_UNEXPECTED_REVERSE_CURRENT UINT32_C(0x00004000)
#define SENSOR_FAULT_SUPPLY_UNSTABLE            UINT32_C(0x00008000)
#define SENSOR_FAULT_CONFIGURATION_ERROR        UINT32_C(0x00010000)

typedef struct {
    sensor_state_t state;
    sensor_fault_flags_t active_faults;
    sensor_fault_flags_t latched_faults;
    uint32_t sample_time_ms;
    uint32_t age_ms;
    uint32_t sequence;
    uint32_t error_count;
    bool has_sample;
} sensor_health_t;

void sensorHealthInitialize(sensor_health_t *health, sensor_state_t state);
void sensorHealthSetState(sensor_health_t *health, sensor_state_t state);
void sensorHealthSetActiveFaults(sensor_health_t *health,
                                 sensor_fault_flags_t faults);
void sensorHealthRaiseFaults(sensor_health_t *health,
                             sensor_fault_flags_t faults);
void sensorHealthClearFaults(sensor_health_t *health,
                             sensor_fault_flags_t faults);
void sensorHealthRecordSample(sensor_health_t *health, uint32_t now_ms);
void sensorHealthUpdateAge(sensor_health_t *health, uint32_t now_ms);
void sensorHealthClearLatched(sensor_health_t *health);
const char *sensorStateName(sensor_state_t state);

#ifdef __cplusplus
}
#endif

#endif
