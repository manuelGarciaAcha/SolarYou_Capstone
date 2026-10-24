#include "system_state.h"
#include "stabilization_task.h"
#include "sensor_api.h"
#include "light_task.h"
#include <math.h>


static const char *TAG = "Light Task";

#define LIGHT_SETPOINT_LIMIT_DEG 45.0f

esp_err_t light_update(system_state_t *state, const stab_config_t *config)
{
    if(state == NULL || config == NULL){
        return ESP_ERR_INVALID_ARG;
    }

    // grab light intensity reading
    sensor_light_sample_t light = {0};
    if(!sensor_adapter_read_light(&light) || !light.valid){
        ESP_LOGW(TAG, "Light sensor read failed");
        return ESP_ERR_INVALID_STATE;
    }

    // Save to system state, converted to target adjustment
    state->roll_light += config->track_k * light.roll_error;
    state->pitch_light += config->track_k * light.pitch_error;

    state->roll_light  = fmaxf(-LIGHT_SETPOINT_LIMIT_DEG,
                               fminf(LIGHT_SETPOINT_LIMIT_DEG, state->roll_light));
    state->pitch_light = fmaxf(-LIGHT_SETPOINT_LIMIT_DEG,
                               fminf(LIGHT_SETPOINT_LIMIT_DEG, state->pitch_light));

    ESP_LOGI("Light_INPUTS", "error roll=%.3f pitch=%.3f | setpoint roll=%.2f pitch=%.2f",
             (double)light.roll_error, (double)light.pitch_error,
             (double)state->roll_light, (double)state->pitch_light);

        ESP_LOGW(TAG, "Light invalid: faults=0x%lx TL=%u TR=%u BL=%u BR=%u",
                 (unsigned long)light.health.active_faults,
                 light.top_left_raw, light.top_right_raw,
                 light.bottom_left_raw, light.bottom_right_raw);

    return ESP_OK;
}
