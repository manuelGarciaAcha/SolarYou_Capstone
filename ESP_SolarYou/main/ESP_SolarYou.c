#include <stdio.h>
#include <stdint.h>
#include "system_state.h"
#include "PID_stabilization.h"
#include "stabilization_task.h"
#include "esp_err.h"
#include "esp_timer.h"

static const char *TAG  = "Main";

pid_controller_t roll_ctlr;
pid_controller_t pitch_ctlr;

static const stab_config_t stab_config = {
    .roll_Kp = 0.5f,
    .roll_Ki = 0.5f,
    .roll_Kd = 0.5f,
    .pitch_Kp = 0.5f,
    .pitch_Ki = 0.5f,
    .pitch_Kd = 0.5f,
    .deriv_time_const = 0.05f,
    .roll_servo_center = 90.0f,
    .pitch_servo_center = 90.0f,
    .pid_cmd_min = -50.0f,
    .pid_cmd_max = 50.0f,
    .task_dt = 0.02f,
};

// placeholder
void gui_dash_init(void)
{
    return;
}

void power_telemetry_init(void)
{
    return;
}

float read_power_placeholder(void){
    return 0;
}

void power_telemetry_update(system_state_t *state)
{   
    float power = read_power_placeholder();
    state->power = power;
    state->power_samples ++;
    state->avg_power += (power - state->avg_power) / state->power_samples;
}

void app_main(void)
{
    system_state_t curr_state;

    // Set system state
    sys_state_reset(&curr_state);


    // Initialize Tasks
    esp_err_t err = stabilization_init(&roll_ctlr, &pitch_ctlr, &stab_config);
    if (err != ESP_OK){
        curr_state.mode = SYSTEM_ERROR;
        ESP_LOGE(TAG, "Stabilization Initialization failed: %s\n", esp_err_to_name(err));
        return;
    }

    gui_dash_init();
    power_telemetry_init();

    // 

    curr_state.mode = SYSTEM_BASIC_CONTROL;
    int64_t curr_time;
    int64_t last_stabilize_time = esp_timer_get_time();
    int64_t last_light_time = esp_timer_get_time();
    int64_t last_telem_time = esp_timer_get_time();


    while(1){
        //curr_state.task = TASK_NONE;
        curr_time = esp_timer_get_time();

        if(curr_time - last_stabilize_time >= STAB_TASK_DT_US){
            last_stabilize_time += STAB_TASK_DT_US;

            stab_output_t stab_output = {0};
            err = stabilization_update(&roll_ctlr, &pitch_ctlr, &stab_output, &stab_config.task_dt);

            if (err != ESP_OK){
                ESP_LOGE(TAG, "Stabilization Task failed: %s\n", esp_err_to_name(err));
                //stabilization_err_cnt ++;

            } else {
                curr_state.roll_imu = stab_output.roll_imu;
                curr_state.pitch_imu = stab_output.pitch_imu;
                curr_state.roll_cmd = stab_output.roll_cmd;
                curr_state.pitch_cmd = stab_output.pitch_cmd;

            }

            curr_time = esp_timer_get_time();
        }

        if(curr_time - last_light_time >= LIGHT_TASK_DT_US){
            last_light_time += LIGHT_TASK_DT_US;

            // light tracking functionality

            //curr_time = esp_timer_get_time();
        }

        if(curr_time - last_telem_time >= TELEM_TASK_DT_US){
            //last_telem_time += TELEM_TASK_DT_US;
               
            //power_telemetry_update(&curr_state);


            // telemetry and power functions

            //curr_time = esp_timer_get_time();
        }
        
    }
}