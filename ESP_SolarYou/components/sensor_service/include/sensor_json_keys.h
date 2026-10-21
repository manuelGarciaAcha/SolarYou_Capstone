#ifndef SENSOR_JSON_KEYS_H
#define SENSOR_JSON_KEYS_H

/* Draft JSON field names.
 * We have to still have to agree to use nested JSON these are purposed possible names. So for now, this is version 0 since it's a draft. */

#define SY_JSON_SCHEMA_VERSION 0U
#define SY_JSON_CONTRACT_STATUS "draft"

#define SY_JSON_KEY_SCHEMA_VERSION "schema_version"
#define SY_JSON_KEY_CONTRACT_STATUS "contract_status"
#define SY_JSON_KEY_DEVICE_UPTIME_MS "device_uptime_ms"
#define SY_JSON_KEY_SEQUENCE "sequence"
#define SY_JSON_KEY_VALID "valid"

#define SY_JSON_KEY_IMU "imu"
#define SY_JSON_KEY_LIGHT "light"
#define SY_JSON_KEY_POWER "power"
#define SY_JSON_KEY_HEALTH "health"

#define SY_JSON_KEY_PITCH_DEG "pitch_deg"
#define SY_JSON_KEY_ROLL_DEG "roll_deg"

#define SY_JSON_KEY_PITCH_ERROR_NORM "pitch_error_norm"
#define SY_JSON_KEY_ROLL_ERROR_NORM "roll_error_norm"
#define SY_JSON_KEY_TOP_LEFT_ADC "top_left_adc"
#define SY_JSON_KEY_TOP_RIGHT_ADC "top_right_adc"
#define SY_JSON_KEY_BOTTOM_LEFT_ADC "bottom_left_adc"
#define SY_JSON_KEY_BOTTOM_RIGHT_ADC "bottom_right_adc"

#define SY_JSON_KEY_LOAD_VOLTAGE_V "load_voltage_v"
#define SY_JSON_KEY_CURRENT_A "current_a"
#define SY_JSON_KEY_POWER_W "power_w"
#define SY_JSON_KEY_SHUNT_VOLTAGE_MV "shunt_voltage_mv"

#define SY_JSON_KEY_STATE "state"
#define SY_JSON_KEY_ACTIVE_FAULTS "active_faults"
#define SY_JSON_KEY_LATCHED_FAULTS "latched_faults"
#define SY_JSON_KEY_SAMPLE_TIME_MS "sample_time_ms"
#define SY_JSON_KEY_AGE_MS "age_ms"
#define SY_JSON_KEY_ERROR_COUNT "error_count"
#define SY_JSON_KEY_HAS_SAMPLE "has_sample"

#endif
