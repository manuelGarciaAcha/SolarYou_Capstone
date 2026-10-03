#include <stdio.h>
#include <stdint.h>
#include "system_state.h"
#include "PID_stabilization.h"
#include "stabilization_task.h"
#include "esp_err.h"
#include "esp_timer.h"


pid_controller_t roll_ctlr;
pid_controller_t pitch_ctlr;

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
    esp_err_t try_stab_init = stabilization_init(&roll_ctlr, &pitch_ctlr);
    if (try_stab_init != ESP_OK){
        curr_state.mode = SYSTEM_ERROR;
        printf("Stabilization Initialization failed: %s\n", esp_err_to_name(try_stab_init));
        return;
    }

    gui_dash_init();
    power_telemetry_init();

    // 

    curr_state.mode = SYSTEM_BASIC_CONTROL;
    int64_t curr_time;
    int64_t last_light_time = 0;
    int64_t last_stabilize_time = 0;
    int64_t last_telem_time = 0;
    int32_t stabilization_err_cnt = 0;

    while(1){
        //curr_state.task = TASK_NONE;
        curr_time = esp_timer_get_time();

        if(curr_time - last_stabilize_time >= STAB_TASK_DT_US){
            last_stabilize_time += STAB_TASK_DT_US;

            stab_output_t stab_output = {0};
            esp_err_t try_stabilization = stabilization_update(&roll_ctlr, &pitch_ctlr, &stab_output);

            if (try_stabilization != ESP_OK){
                printf("Motor command failed: %s\n", esp_err_to_name(try_stabilization));
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

            curr_time = esp_timer_get_time();
        }

        if(curr_time - last_telem_time >= TELEM_TASK_DT_US){
            last_telem_time += TELEM_TASK_DT_US;
               
            power_telemetry_update(&curr_state);


            // telemetry and power functions

            curr_time = esp_timer_get_time();
        }
        
    }
}