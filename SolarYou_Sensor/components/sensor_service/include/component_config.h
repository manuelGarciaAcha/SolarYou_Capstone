#ifndef COMPONENT_CONFIG_H
#define COMPONENT_CONFIG_H

#define SY_SENSOR_SCHEMA_VERSION 1U
#define SY_SENSOR_POLL_PERIOD_MS 100U
/* Checks sensors 10 times a second, but prints the usual update once a second. */
#define SY_CONSOLE_REPORT_PERIOD_MS 1000U
/* We can turn JSON on when needed, but leave it off for now for a readable test output. */
#define SY_CONSOLE_PRINT_JSON 0

/* Both I2C sensors share these pins on the esp. */
#define SENSOR_I2C_SDA_GPIO 47
#define SENSOR_I2C_SCL_GPIO 48
#define IMU_I2C_ADDRESS 0x4A
#define POWER_I2C_ADDRESS 0x40

#define IMU_REPORT_INTERVAL_US 20000
#define IMU_READ_TIMEOUT_MS 250
#define IMU_NEUTRAL_SAMPLES 50
#define IMU_CAL_MAX_SPREAD_DEG 0.75f
#define IMU_PITCH_DEADBAND_DEG 0.25f
#define IMU_ROLL_DEADBAND_DEG 0.25f
#define IMU_PITCH_SIGN 1.0f
#define IMU_ROLL_SIGN 1.0f

/* GPIO3 affects esp startup, so I still need to coldboot test with the sensor dark, bright, and disconnected. GPIO4/5/6 are the other available ADC1
  choices on this board. If GPIO3 is what's causing the issues I'm currently having, maybe using an external ADC could help. */
#define LIGHT_TOP_LEFT_GPIO 3
#define LIGHT_TOP_RIGHT_GPIO 4
#define LIGHT_BOTTOM_LEFT_GPIO 5
#define LIGHT_BOTTOM_RIGHT_GPIO 6
#define LIGHT_AVERAGE_SETS 16
#define LIGHT_NEUTRAL_SAMPLES 50
#define LIGHT_CAL_MAX_SPREAD 0.08f
#define LIGHT_DEADBAND 0.02f
#define LIGHT_MIN_TOTAL_COUNT 400
#define LIGHT_SATURATION_COUNT 4000
#define LIGHT_OPEN_COUNT 8
#define LIGHT_SHORT_COUNT 4087
/* Doen't accept a very uneven light setup as the neutral reference. */
#define LIGHT_CAL_MAX_ABS_ERROR 0.95f
#define LIGHT_PITCH_SIGN 1.0f
#define LIGHT_ROLL_SIGN 1.0f

#define POWER_MAX_BUS_VOLTAGE_V 26.0f
#define POWER_MAX_CURRENT_A 2.0f
#define POWER_REVERSE_FAULT_A -0.01f
/* After five missed 100 ms reads, the last good value is stale. */
#define POWER_STALE_TIMEOUT_MS 500U

/* These pins are just for Wokwi controls, not the real esp wiring. */
#define IMU_SIM_PITCH_GPIO 7
#define IMU_SIM_ROLL_GPIO 8
#define IMU_SIM_TIMEOUT_GPIO 11
#define IMU_SIM_MAX_ANGLE_DEG 45.0f

#define POWER_SIM_VOLTAGE_GPIO 9
#define POWER_SIM_CURRENT_GPIO 10
#define POWER_SIM_COMM_FAULT_GPIO 12
#define POWER_SIM_MAX_VOLTAGE_V 30.0f
#define POWER_SIM_MIN_CURRENT_A -0.50f
#define POWER_SIM_MAX_CURRENT_A 2.50f
#define POWER_SIM_SHUNT_OHMS 0.10f

#endif
