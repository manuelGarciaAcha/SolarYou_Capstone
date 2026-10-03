#ifndef STABILIZATION_TASK_H_INCLUDED
#define STABILIZATION_TASK_H_INCLUDED

#include "esp_err.h"
#include "motor_control.h"
#include "PID_stabilization.h"

typedef struct {
    float roll_imu;
    float pitch_imu;
    float roll_cmd;
    float pitch_cmd;
} stab_output_t;

esp_err_t stabilization_init(pid_controller_t *roll, pid_controller_t *pitch);
void stabilization_reset(pid_controller_t *roll, pid_controller_t *pitch);
esp_err_t stabilization_update(pid_controller_t *roll, pid_controller_t *pitch, stab_output_t *output);
#endif