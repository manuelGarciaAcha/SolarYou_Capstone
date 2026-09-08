
#define PID_DT 0.010f


typedef struct {
    float Kp;                   // proportional gain constant
    float Ki;                   // integral gain constant
    float Kd;                   // derivative gain constant
    float prev_err;             // Previous Error
    float integral_accum_err;   // Accumulated Integral Error
} PID_controller;


float pid_calculate (PID_controller *pid, float measured_angle, float target_angle)
{
    float err;
    float command;

    // Error Calculation
    err = target_angle - measured_angle;

    pid->integral_accum_err += err * PID_DT;

    // PID Command Calculation
    float proportional = pid->Kp * err;
    float integral = pid->Ki * pid->integral_accum_err;
    float derivative = pid->Kd * ((err - pid->prev_err)/PID_DT);

    command = proportional + integral + derivative;
    
    pid->prev_err = err;
    
    return command;
}