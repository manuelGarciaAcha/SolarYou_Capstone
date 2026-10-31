#include <stdio.h>
#include <math.h>
#include "power_n_telemetry.h"
#include "esp_err.h"
#include "sensor_api.h"
#include "esp_log.h"

static const char *TAG = "POWER AND TELEMETRY";


void uart_telemetry_send(const system_state_t *state)
{
    if (state == NULL) {
        return;
    }

    if(state->power_valid){
        printf(
        "{\"pitch\":%.2f,\"roll\":%.2f,\"voltage\":%.3f,\"current\":%.3f,\"power\":%.3f,\"avg power\":%.3f,}\n",
        (double)state->pitch_imu,
        (double)state->roll_imu,
        (double)state->voltage,
        (double)state->current,
        (double)state->power,
        (double)state->avg_power
        );
    
    }else{
        printf(
        "{\"pitch\":%.2f,\"roll\":%.2f,\"voltage\":null,\"current\":null,\"power\":null,\"avg power\":null,}\n",
        (double)state->pitch_imu,
        (double)state->roll_imu
        );

    }
    fflush(stdout);
}

esp_err_t power_update(system_state_t *state)
{
    if (state == NULL){
        return ESP_ERR_INVALID_ARG;
    }

    state->power_valid = false;

    sensor_power_sample_t sample = {0};
    if (!sensor_adapter_read_power(&sample) || !sample.valid){
        ESP_LOGW(TAG, "Power sensor read failed");
        return ESP_ERR_INVALID_STATE;
    }

    if(!isfinite(sample.voltage_v) || !isfinite(sample.current_a) || !isfinite(sample.power_w)){
        ESP_LOGW(TAG, "Power sensor value error");
        return ESP_ERR_INVALID_RESPONSE;
    }

    state->voltage = sample.voltage_v;
    state->current = sample.current_a;
    state->power = sample.power_w;

    state->power_samples ++;

    state->avg_power += (sample.power_w - state->avg_power)/(float)state->power_samples;

    state->power_valid = true;
    return ESP_OK;
}
