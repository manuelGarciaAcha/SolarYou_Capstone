#ifndef STABILIZATION_TASK_H_INCLUDED
#define STABILIZATION_TASK_H_INCLUDED

#include "esp_err.h"
#include "motor_control.h"
#include "PID_stabilization.h"
#include "esp_log.h"

typedef struct {
    float roll_imu;
    float pitch_imu;
    float roll_cmd;
    float pitch_cmd;
} stab_output_t;

typedef struct {
    // PID gains
    float roll_Kp;
    float roll_Ki;
    float roll_Kd;
    float pitch_Kp;
    float pitch_Ki;
    float pitch_Kd;

    // PID tunable parameters
    float deriv_time_const;
    float roll_servo_center;
    float pitch_servo_center;
    float pid_cmd_min;
    float pid_cmd_max;
    float task_dt;

    // Solar tracking gain
    float track_k;
    
} stab_config_t;

esp_err_t stabilization_init(pid_controller_t *roll, pid_controller_t *pitch, const stab_config_t *config);
esp_err_t stabilization_reset(pid_controller_t *roll, pid_controller_t *pitch);
esp_err_t stabilization_update(pid_controller_t *roll, pid_controller_t *pitch, stab_output_t *output, const float *dt, float roll_light_angle, float pitch_light_angle);
#endif