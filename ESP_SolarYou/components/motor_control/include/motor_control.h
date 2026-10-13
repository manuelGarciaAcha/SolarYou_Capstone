#ifndef MOTOR_CONTROL_H_INCLUDED
#define MOTOR_CONTROL_H_INCLUDED

#include "esp_err.h"

#define MAX_SERVO_ANGLE_ROLL        170
#define MIN_SERVO_ANGLE_ROLL        30
#define MAX_SERVO_ANGLE_PITCH       120 
#define MIN_SERVO_ANGLE_PITCH       0

esp_err_t motor_init(void);
esp_err_t command_motor_angle(float roll_angle, float pitch_angle);
esp_err_t motor_del(void);

#endif
