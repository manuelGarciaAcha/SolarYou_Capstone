#ifndef SENSOR_API_H
#define SENSOR_API_H

#include <stdbool.h>
#include <stdint.h>

#include "component_config.h"
#include "sensor_status.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool valid;
    float pitch_deg;
    float roll_deg;
    sensor_health_t health;
} sensor_imu_sample_t;

typedef struct {
    bool valid;
    float pitch_error;
    float roll_error;
    uint16_t top_left_raw;
    uint16_t top_right_raw;
    uint16_t bottom_left_raw;
    uint16_t bottom_right_raw;
    sensor_health_t health;
} sensor_light_sample_t;

typedef struct {
    bool valid;
    float voltage_v;
    float current_a;
    float power_w;
    float shunt_voltage_mv;
    sensor_health_t health;
} sensor_power_sample_t;

typedef struct {
    uint32_t schema_version;
    uint32_t device_uptime_ms;
    uint32_t sequence;
    bool valid;
    sensor_imu_sample_t imu;
    sensor_light_sample_t light;
    sensor_power_sample_t power;
} sensor_snapshot_t;

bool sensor_adapter_init(void);

/* These are separate so an IMU reset does not also change the saved light balance. */
bool sensor_adapter_capture_imu_neutral(void);
bool sensor_adapter_capture_light_neutral(void);

/* Convenience function for tests that need to capture both references together. */
bool sensor_adapter_capture_neutral(void);

/* Each function below performs one real sensor read and returns only that
 * sensor's values, validity, timing, and health information. This lets the
 * integration code poll the sensors on separate schedules. The IMU and power
 * sensor share the I2C bus. Until it is decided where the shared bus mutex
 * belongs, callers must not run those two reads at the same time from
 * different tasks. */
bool sensor_adapter_read_imu(sensor_imu_sample_t *sample);
bool sensor_adapter_read_light(sensor_light_sample_t *sample);
bool sensor_adapter_read_power(sensor_power_sample_t *sample);

/* Convenience read for the combined validation, debug, and JSON paths. */
bool sensor_adapter_read(sensor_snapshot_t *snapshot);
void sensor_adapter_clear_latched_faults(void);

#ifdef __cplusplus
}
#endif

#endif
