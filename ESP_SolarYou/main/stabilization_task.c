#include "stabilization_task.h"

#define T_C                 0.050f
#define ROLL_SERVO_CENTER   90.0f       //to be configured to actual servo neutral in phsyical implementation
#define PITCH_SERVO_CENTER  90.0f      //to be configured to actual servo neutral in phsyical implementation


// define PID gain values
static float Kp = 0.5;
static float Ki = 0.5;
static float Kd = 0.5;


// --------------------------------- //
// Placeholder functions (JENNIFER)

void imu_init(void)
{
    return;
}

void light_init(void)
{
    return;
}

float imu_get_roll(void)
{
    return 0.0f;
}

float imu_get_pitch(void)
{
    return 0.0f;
}

float light_get_roll(void)
{
    return 0.0f;
}

float light_get_pitch(void)
{
    return 0.0f;
}

// --------------------------------- //

esp_err_t stabilization_init(pid_controller_t *roll, pid_controller_t *pitch)
{
    
    esp_err_t try_motor_init = motor_init();
    if (try_motor_init != ESP_OK){
        return try_motor_init;
    }

    pid_init(roll);
    pid_init(pitch);

    imu_init();
    light_init();

    pid_tune(roll, Kp, Ki, Kd, T_C);
    pid_tune(pitch, Kp, Ki, Kd, T_C);

    return ESP_OK;
}


void stabilization_reset(pid_controller_t *roll, pid_controller_t *pitch)
{
    pid_reset(roll);
    pid_reset(pitch);
}

esp_err_t stabilization_update(pid_controller_t *roll, pid_controller_t *pitch, stab_output_t *output)
{
    // fetch measured(IMU) and target(light) angles
    float roll_imu_angle = imu_get_roll();         //placeholder;
    float pitch_imu_angle = imu_get_pitch();       //placeholder;

    float roll_light_angle = light_get_roll();     //placeholder
    float pitch_light_angle = light_get_pitch();   //placeholder

    float roll_offset_baseline = roll_light_angle + ROLL_SERVO_CENTER;
    float pitch_offset_baseline = pitch_light_angle + PITCH_SERVO_CENTER;

    // determine PID command
    float roll_command = pid_calculate(roll, roll_imu_angle, roll_light_angle);
    float pitch_command = pid_calculate(pitch, pitch_imu_angle, pitch_light_angle);

    // map PID command angles to servo angles
    float servo_roll_angle = roll_command + roll_offset_baseline;
    float servo_pitch_angle = pitch_command + pitch_offset_baseline;

    esp_err_t try_command_motor = command_motor_angle(servo_roll_angle, servo_pitch_angle);
    if (try_command_motor != ESP_OK){
        return try_command_motor;
    }

    output->roll_imu = roll_imu_angle;
    output->pitch_imu = pitch_imu_angle;
    output->roll_cmd = roll_command;
    output->pitch_cmd = pitch_command;

    return ESP_OK;
}