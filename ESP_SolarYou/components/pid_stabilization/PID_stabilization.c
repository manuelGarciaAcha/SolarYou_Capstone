/*
    Parameters that require adjustment based on Physical Implementation:
        - PID gain constants: Kp, Ki, Kd
        - Derivative filtering time constant
*/

#include "PID_stabilization.h"
#include <math.h>
#include <string.h>
#include "esp_log.h"

static const char *TAG = "PID";

esp_err_t pid_init (pid_controller_t *pid, 
                const float Kp, 
                const float Ki, 
                const float Kd,
                const float T_C,
                const float output_min,
                const float output_max,
                const float servo_center)
{
    if(pid == NULL){
        return ESP_ERR_INVALID_ARG;
    }

    // temp init values for pid 
    memset(pid, 0, sizeof(*pid));

    if (!isfinite(Kp) || 
        !isfinite(Ki) || 
        !isfinite(Kd) || 
        !isfinite(T_C) ||
        !isfinite(output_min) ||
        !isfinite(output_max) ||
        !isfinite(servo_center) ||
        T_C <= 0.0f ||
        output_min >= output_max)
    {
        return ESP_ERR_INVALID_ARG;
    }

    pid->Kp = Kp;
    pid->Ki = Ki;
    pid->Kd = Kd;
    pid->T_C = T_C;
    pid->cmd_cap_min = output_min;
    pid->cmd_cap_max = output_max;
    pid->servo_center = servo_center;
    pid->deriv_init = true;

    return ESP_OK;
}

esp_err_t pid_tune (pid_controller_t *pid, 
                const float Kp, 
                const float Ki, 
                const float Kd, 
                const float T_C,
                const float output_min,
                const float output_max,
                const float servo_center)
{
    if (!isfinite(Kp) || 
        !isfinite(Ki) || 
        !isfinite(Kd) || 
        !isfinite(T_C) ||
        !isfinite(output_min) ||
        !isfinite(output_max) ||
        !isfinite(servo_center) ||
        T_C <= 0.0f || 
        output_min >= output_max)
    {
        ESP_LOGW(TAG, "Tuning failed: %s\n", esp_err_to_name(ESP_ERR_INVALID_ARG));
        return ESP_ERR_INVALID_ARG;
    }

    pid->Kp = Kp;
    pid->Ki = Ki;
    pid->Kd = Kd;
    pid->T_C = T_C;
    pid->cmd_cap_min = output_min;
    pid->cmd_cap_max = output_max;
    pid->servo_center = servo_center;

    return ESP_OK;
}

esp_err_t pid_calculate (pid_controller_t *pid, 
                    float measured_angle, 
                    float target_angle,
                    float *output,
                    float dt)
{
    // input sanity check
    if (pid == NULL || output == NULL){
        return ESP_ERR_INVALID_ARG;
    } 
    
    if (!isfinite(target_angle) || !isfinite(measured_angle) || !isfinite(dt) || dt <= 0.0f){
        return ESP_ERR_INVALID_ARG;
    }

    float err;
    float command;

    // Error 
    err = target_angle - measured_angle;

    // Proportional component calculation
    float proportional = err;

    // Integral error accumulation 
    float old_accum = pid->integral_accum_err;
    float candidate_accum = old_accum + err * dt;

    // Derivative first pass priming
    float derivative = 0.0f;
    if (pid->deriv_init){

        // prime
        pid->prev_measurement = measured_angle;
        pid->prev_deriv = 0.0f;
        pid->deriv_init = false;
    }else{

        // Derivative calculation with low-pass filter for IMU noise
        derivative = (-(measured_angle - pid->prev_measurement) + (pid->T_C * pid->prev_deriv))/(dt + pid->T_C);
        
        // set values for next iteration
        pid->prev_measurement = measured_angle;
        pid->prev_deriv = derivative;
    }


    // Candidate command calculation with saturation check
    command = (pid->Kp * proportional) + (pid->Ki * candidate_accum) + (pid->Kd * derivative);
    
    // anti-windup logic -- integral value accumulation protection
    if ((command > pid->cmd_cap_max) && (err > 0)){
        command = (pid->Kp * proportional) + (pid->Ki * old_accum) + (pid->Kd * derivative);
        pid->integral_accum_err = old_accum;
    } 
    else if ((command < pid->cmd_cap_min) && (err < 0)){
        command = (pid->Kp * proportional) + (pid->Ki * old_accum) + (pid->Kd * derivative);
        pid->integral_accum_err = old_accum;
    } 
    else {
        pid->integral_accum_err = candidate_accum;
    }

    // command saturation protection
    if (command < pid->cmd_cap_min){
        command = pid->cmd_cap_min;
    }
    else if(command > pid->cmd_cap_max){
        command = pid->cmd_cap_max;
    }
    
    *output = command;
    return ESP_OK;
}

esp_err_t pid_reset (pid_controller_t *pid)
{
    if (pid == NULL){
        return ESP_ERR_INVALID_ARG;
    }

    pid->prev_measurement = 0.0f;
    pid->integral_accum_err = 0.0f;
    pid->prev_deriv = 0.0f;
    pid->deriv_init = true;

    return ESP_OK;
}