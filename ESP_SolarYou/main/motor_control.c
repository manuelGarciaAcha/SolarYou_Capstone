#include "iot_servo.h"
#include "motor_control.h"

#define MAX_SERVO_ANGLE     140 //temp value
#define MIN_SERVO_ANGLE     40  //temp value  
#define SERVO_CH0_PIN       0   //temp value
#define SERVO_CH1_PIN       1   //temp value


// define servos
static servo_handle_t roll_servo = NULL;
static servo_handle_t pitch_servo = NULL;

esp_err_t motor_init(void)
{
    if (roll_servo || pitch_servo) return ESP_ERR_INVALID_STATE;

    //configure roll servo to LEDC Low, provision LEDC channel and GPIO 
    servo_config_t servo_cfg = SERVO_CONFIG_DEFAULT(LEDC_LOW_SPEED_MODE, LEDC_TIMER_0, LEDC_CHANNEL_0, SERVO_CH0_PIN);
    
    esp_err_t try_new_servo = iot_servo_new(&servo_cfg, &roll_servo);
    if (try_new_servo != ESP_OK){
        return try_new_servo;
    }

    //configure pitch servo to LEDC Low, provision LEDC channel and GPIO
    servo_cfg.channel = LEDC_CHANNEL_1;
    servo_cfg.gpio_num = SERVO_CH1_PIN;

    try_new_servo = iot_servo_new(&servo_cfg, &pitch_servo);
    if (try_new_servo != ESP_OK){
        iot_servo_del(roll_servo);
        roll_servo = NULL;
        return try_new_servo;
    }

    return ESP_OK;
}


// abstracted motor movement function
esp_err_t roll_pitch_angle_set(float roll_angle, float pitch_angle)
{
    if ((roll_angle < MIN_SERVO_ANGLE) || (roll_angle > MAX_SERVO_ANGLE)){
        return ESP_ERR_INVALID_ARG;
    }

    if ((pitch_angle < MIN_SERVO_ANGLE) || (pitch_angle > MAX_SERVO_ANGLE)){
        return ESP_ERR_INVALID_ARG;
    }

// might need review since it bails on roll fail (is that bad given next iter?)
    esp_err_t try_roll = iot_servo_write_angle(roll_servo, roll_angle);
    if (try_roll != ESP_OK){
        return try_roll;
    }

    esp_err_t try_pitch = iot_servo_write_angle(pitch_servo, pitch_angle);
    if (try_pitch != ESP_OK){
        return try_pitch;
    }

    return ESP_OK;
}

esp_err_t motor_del(void)
{
    esp_err_t err = ESP_OK;

    if (roll_servo) {
        esp_err_t r = iot_servo_del(roll_servo);
        if (r != ESP_OK) err = r;
        roll_servo = NULL;
    }

    if (pitch_servo) {
        esp_err_t r = iot_servo_del(pitch_servo);
        if (r != ESP_OK) err = r;
        pitch_servo = NULL;
    }

    return err;
}