#include "system_state.h"
#include "stabilization_task.h"
#include "sensor_api.h"
#include "light_task.h"


static const char *TAG = "Light Task";


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


    return ESP_OK;
}