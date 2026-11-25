#include <stdio.h>
#include <math.h>
#include "power_n_telemetry.h"
#include "esp_err.h"
#include "sensor_api.h"
#include "esp_log.h"
#include "sdkconfig.h"

static const char *TAG = "POWER AND TELEMETRY";


void uart_telemetry_send(const system_state_t *state)
{
    if (state == NULL) {
        return;
    }

    char imu_json[128];
    if (state->imu_valid && isfinite(state->pitch_imu) && isfinite(state->roll_imu)) {
        snprintf(imu_json, sizeof(imu_json), "\"pitch\":%.2f,\"roll\":%.2f",
                 (double)state->pitch_imu, (double)state->roll_imu);
    } else {
        snprintf(imu_json, sizeof(imu_json), "\"pitch\":null,\"roll\":null");
    }

    char power_json[128];
    if (state->power_valid) {
        snprintf(power_json, sizeof(power_json),
                 "\"voltage\":%.3f,\"current\":%.3f,\"power\":%.3f,\"avg_power\":%.3f",
                 (double)state->voltage, (double)state->current,
                 (double)state->power, (double)state->avg_power);
    } else {
        snprintf(power_json, sizeof(power_json),
                 "\"voltage\":null,\"current\":null,\"power\":null,\"avg_power\":null");
    }

#if defined(CONFIG_SOLARYOU_RUNTIME_FULL_CONTROL) && CONFIG_SOLARYOU_RUNTIME_FULL_CONTROL
    // roll_light/pitch_light are the tracking target angles (degrees)
    printf("{%s,%s,\"pitch_target\":%.2f,\"roll_target\":%.2f}\n",
           imu_json, power_json,
           (double)state->pitch_light, (double)state->roll_light);
#else
    printf("{%s,%s}\n", imu_json, power_json);
#endif
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
