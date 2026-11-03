#include "system_state.h"
#include "stabilization_task.h"
#include "sensor_api.h"
#include "light_task.h"
#include <math.h>


static const char *TAG = "Light Task";

#define LIGHT_ROLL_LIMIT_DEG   45.0f
#define LIGHT_PITCH_LIMIT_DEG  35.0f

esp_err_t light_update(system_state_t *state, const stab_config_t *config, float dt)
{
    if(state == NULL || config == NULL){
        return ESP_ERR_INVALID_ARG;
    }

    // grab light intensity reading
    sensor_light_sample_t light = {0};
    bool read_ok = sensor_adapter_read_light(&light);

    // log AFTER the read so the values are real
    ESP_LOGI("Light_RAW", "TL=%u TR=%u BL=%u BR=%u | valid=%d faults=0x%x",
             (unsigned)light.top_left_raw,  (unsigned)light.top_right_raw,
             (unsigned)light.bottom_left_raw, (unsigned)light.bottom_right_raw,
             light.valid, (unsigned)light.health.active_faults);

    if(!read_ok || !light.valid){
        ESP_LOGW(TAG, "Light sensor read failed");
        return ESP_ERR_INVALID_STATE;
    }

    // Save to system state, converted to target adjustment
    state->roll_light += config->track_k * light.roll_error;
    state->pitch_light += config->track_k * light.pitch_error;

    state->roll_light  = fmaxf(-LIGHT_ROLL_LIMIT_DEG,  fminf(LIGHT_ROLL_LIMIT_DEG,  state->roll_light));
    state->pitch_light = fmaxf(-LIGHT_PITCH_LIMIT_DEG, fminf(LIGHT_PITCH_LIMIT_DEG, state->pitch_light));

    ESP_LOGI("Light_INPUTS", "error roll=%.3f pitch=%.3f | setpoint roll=%.2f pitch=%.2f",
             (double)light.roll_error, (double)light.pitch_error,
             (double)state->roll_light, (double)state->pitch_light);

    return ESP_OK;
}
