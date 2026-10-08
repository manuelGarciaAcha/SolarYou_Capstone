#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

#if CONFIG_IDF_TARGET_ESP32

#define SENSOR_I2C_SDA_GPIO 21
#define SENSOR_I2C_SCL_GPIO 22

#define SERVO_CH0_PIN       33
#define SERVO_CH1_PIN       32

#define LIGHT_TOP_LEFT_GPIO      34
#define LIGHT_TOP_RIGHT_GPIO     35
#define LIGHT_BOTTOM_LEFT_GPIO   36
#define LIGHT_BOTTOM_RIGHT_GPIO  39

#elif CONFIG_IDF_TARGET_ESP32S3

#define SENSOR_I2C_SDA_GPIO 47
#define SENSOR_I2C_SCL_GPIO 48

// may require changes based on board type
#define SERVO_CH0_PIN       33   // temp
#define SERVO_CH1_PIN       32   // temp

#define LIGHT_TOP_LEFT_GPIO      3
#define LIGHT_TOP_RIGHT_GPIO     4
#define LIGHT_BOTTOM_LEFT_GPIO   5
#define LIGHT_BOTTOM_RIGHT_GPIO  6

#else
#error "Unsupported ESP32 target"
#endif

#endif