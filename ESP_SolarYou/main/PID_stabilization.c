
/*
    Parameters that require adjustment based on Physical Implementation:
        - PID gain constants: Kp, Ki, Kd
        - Derivative filtering time constant
*/


#define PID_DT      0.010f  // example value


typedef struct {
    // Basic PID
    float Kp;                   // proportional gain constant
    float Ki;                   // integral gain constant
    float Kd;                   // derivative gain constant
    float prev_err;             // Previous Error
    float integral_accum_err;   // Accumulated Integral Error
    float max;                  // Maximum possible Position Command
    float min;                  // Minimum possible Position Command

    // Derivative Noise Filtering
    float T_C;                  // Derivative Filter Time Constant
    float prev_deriv;           // Previous derivative value

    // Integral Anti-windup


} PID_controller;

void pid_init (PID_controller *pid){
    // temp init values for pid 
    
    pid->prev_err = 0;             
    pid->integral_accum_err = 0;   
    pid->max = 50;                  // assumed centerpoint 90deg              
    pid->min = -50;                      
    pid->T_C = 0.050f;              // to be tuned  
    pid->prev_deriv = 0; 
}


float pid_calculate (PID_controller *pid, float measured_angle, float target_angle)
{
    float err;
    float command;

    // Error 
    err = target_angle - measured_angle;

    // Integral error accumulation 
    float old_accum = pid->integral_accum_err;
    float candidate_accum = old_accum + err * PID_DT;

    // PID component 
    float proportional = err;

    // Integral calculation for anti-windup conditional logic
    float candidate_integral = candidate_accum;
    float old_integral = old_accum;

    // Derivative calculation with low-pass filter for IMU noise
    float derivative = (err - pid->prev_err + (pid->T_C * pid->prev_deriv))/(PID_DT + pid->T_C);

    // Candidate command calculation with saturation check
        command = (pid->Kp * proportional) + (pid->Ki * candidate_integral) + (pid->Kd * derivative);
    
    if ((command > pid->max) && (err > 0)){
        command = (pid->Kp * proportional) + (pid->Ki * old_integral) + (pid->Kd * derivative);
        pid->integral_accum_err = old_accum;
    } 
    else if ((command < pid->min) && (err < 0)){
        command = (pid->Kp * proportional) + (pid->Ki * old_integral) + (pid->Kd * derivative);
        pid->integral_accum_err = old_accum;
    } 
    else {
        pid->integral_accum_err = candidate_accum;
    }

    // command saturation protection
    if (command > pid->max){
        command = pid->max;
    }
    else if(command < pid->min){
        command = pid->min;
    }

    // set values for next iteration
    pid->prev_err = err;
    pid->prev_deriv = derivative;
    
    return command;
}