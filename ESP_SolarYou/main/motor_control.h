#ifndef MOTOR_CONTROL_H_INCLUDED
#define MOTOR_CONTROL_H_INCLUDED

#include "esp_err.h"

esp_err_t motor_init(void);
esp_err_t roll_pitch_angle_set(float roll_angle, float pitch_angle);
esp_err_t motor_del(void);

#endif