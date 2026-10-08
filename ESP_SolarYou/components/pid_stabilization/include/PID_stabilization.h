#ifndef PID_STABILIZATION_H_INCLUDED
#define PID_STABILIZATION_H_INCLUDED

#include <stdbool.h>
#include "esp_err.h"

typedef struct {

    // Basic PID
    float Kp;                   // proportional gain constant
    float Ki;                   // integral gain constant
    float Kd;                   // derivative gain constant
    float prev_measurement;
    float integral_accum_err;   // Accumulated Integral Error

    // Derivative Noise Filtering
    float T_C;                  // Derivative Filter Time Constant
    float prev_deriv;           // Previous derivative value
    bool deriv_init;            // derivative priming necessity conditional-> true = first pass

    // PID output clamping
    float cmd_cap_max;
    float cmd_cap_min;

    float servo_center;
} pid_controller_t;

esp_err_t pid_init (pid_controller_t *pid, 
                const float Kp, 
                const float Ki, 
                const float Kd,
                const float T_C,
                const float output_min,
                const float output_max,
                const float servo_center);

esp_err_t pid_tune (pid_controller_t *pid, 
                const float Kp, 
                const float Ki, 
                const float Kd, 
                const float T_C,
                const float output_min,
                const float output_max,
                const float servo_center);

esp_err_t pid_calculate (pid_controller_t *pid, 
                    float measured_angle, 
                    float target_angle,
                    float *output,
                    float dt);

esp_err_t pid_reset (pid_controller_t *pid);

#endif