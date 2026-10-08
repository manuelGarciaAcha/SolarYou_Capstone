#include "stabilization_task.h"

static const char *TAG = "Stabilization";

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

esp_err_t stabilization_init(pid_controller_t *roll, pid_controller_t *pitch, const stab_config_t *config)
{
    if(roll == NULL || pitch == NULL || config == NULL){
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = motor_init();
    if (err != ESP_OK){
        ESP_LOGE(TAG, "Motor initalization failed %s\n", esp_err_to_name(err));
        return err;
    }

    err  = pid_init (roll, config->roll_Kp, config->roll_Ki, config->roll_Kd, config->deriv_time_const, config->pid_cmd_min, config->pid_cmd_max, config->roll_servo_center);
    if (err != ESP_OK){
        ESP_LOGE(TAG, "Roll PID controller initalization failed %s\n", esp_err_to_name(err));
        return err;
    }

    err = pid_init (pitch, config->pitch_Kp, config->pitch_Ki, config->pitch_Kd, config->deriv_time_const, config->pid_cmd_min, config->pid_cmd_max, config->pitch_servo_center);
    if (err != ESP_OK){
        ESP_LOGE(TAG, "Pitch PID controller initalization failed %s\n", esp_err_to_name(err));
        return err;
    }


    imu_init();
    light_init();

    ESP_LOGI(TAG, "Initialization Successul");
    return ESP_OK;
}


esp_err_t stabilization_reset(pid_controller_t *roll, pid_controller_t *pitch)
{
    esp_err_t err = pid_reset(roll);
    if (err != ESP_OK){
        ESP_LOGE(TAG, "PID reset failed %s\n", esp_err_to_name(err));
        return err;
    }
    
    err = pid_reset(pitch);
    if (err != ESP_OK){
        ESP_LOGE(TAG, "PID reset failed %s\n", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Reset Successful");
    return ESP_OK;
}

esp_err_t stabilization_update(pid_controller_t *roll, pid_controller_t *pitch, stab_output_t *output, const float *dt)
{
    if(output == NULL || dt == NULL){
        return ESP_ERR_INVALID_ARG;
    }

    float roll_cmd;
    float pitch_cmd;

    // fetch measured(IMU) and target(light) angles
    float roll_imu_angle = imu_get_roll();         //placeholder;
    float pitch_imu_angle = imu_get_pitch();       //placeholder;

    float roll_light_angle = light_get_roll();     //placeholder
    float pitch_light_angle = light_get_pitch();   //placeholder

    float roll_offset_baseline = roll_light_angle + roll->servo_center;
    float pitch_offset_baseline = pitch_light_angle + pitch->servo_center;

    // determine PID command
    esp_err_t err = pid_calculate(roll, roll_imu_angle, roll_light_angle, &roll_cmd, *dt);

    if(err != ESP_OK){
        ESP_LOGW(TAG, "Roll PID skipped, calculation error: %s\n", esp_err_to_name(err));
        return err;
    }

    err = pid_calculate(pitch, pitch_imu_angle, pitch_light_angle, &pitch_cmd, *dt);

    if(err != ESP_OK){
        ESP_LOGW(TAG, "Pitch PID skipped, calculation error: %s\n", esp_err_to_name(err));
        return err;
    }

    // map PID command angles to servo angles
    float servo_roll_angle = roll_cmd + roll_offset_baseline;
    float servo_pitch_angle = pitch_cmd + pitch_offset_baseline;

    err = command_motor_angle(servo_roll_angle, servo_pitch_angle);
    if (err != ESP_OK){
        return err;
    }

    output->roll_imu = roll_imu_angle;
    output->pitch_imu = pitch_imu_angle;
    output->roll_cmd = roll_cmd;
    output->pitch_cmd = pitch_cmd;

    return ESP_OK;
}