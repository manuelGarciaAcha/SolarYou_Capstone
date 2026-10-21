#include <stdio.h>
#include <stdint.h>
#include "system_state.h"
#include "PID_stabilization.h"
#include "stabilization_task.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sensor_api.h"
#include "light_task.h"
#include "uart_telemetry.h"
#include "sdkconfig.h"

static const char *TAG  = "Main";

system_state_t curr_state;

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
    .roll_servo_center = 100.0f,
    .pitch_servo_center = 60.0f,
    .pid_cmd_min = -50.0f,
    .pid_cmd_max = 50.0f,
    .task_dt = STAB_TASK_DT_US / 1000000.0f,
    .track_k = 2.0f,
};

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

static void log_sensor_init_result(const char *name, bool enabled,
                                   bool initialized, bool required)
{
    if (!enabled) {
        if (required) {
            ESP_LOGE(TAG, "%s: DISABLED (required in this mode)", name);
        } else {
            ESP_LOGI(TAG, "%s: DISABLED (optional in this mode)", name);
        }
    } else if (initialized) {
        ESP_LOGI(TAG, "%s: PASS", name);
    } else if (required) {
        ESP_LOGE(TAG, "%s: FAIL (required in this mode)", name);
    } else {
        ESP_LOGW(TAG, "%s: UNAVAILABLE (optional in this mode)", name);
    }
}

#if !defined(CONFIG_SOLARYOU_RUNTIME_FULL_CONTROL) || !CONFIG_SOLARYOU_RUNTIME_FULL_CONTROL
static esp_err_t sensor_dashboard_update_imu(system_state_t *state)
{
    if (state == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    sensor_imu_sample_t imu = {0};
    if (!sensor_adapter_read_imu(&imu) || !imu.valid) {
        return ESP_ERR_INVALID_STATE;
    }

    state->roll_imu = imu.roll_deg;
    state->pitch_imu = imu.pitch_deg;
    state->roll_cmd = 0.0f;
    state->pitch_cmd = 0.0f;
    return ESP_OK;
}
#endif

void app_main(void)
{

    // Set system state
    sys_state_reset(&curr_state);

    // Initialize every configured sensor and report each result separately.
    const bool all_selected_sensors_ready = sensor_adapter_init();
    sensor_init_status_t sensor_status = {0};
    if (!sensor_adapter_get_init_status(&sensor_status)) {
        curr_state.mode = SYSTEM_ERROR;
        ESP_LOGE(TAG, "Could not read the sensor initialization report");
        return;
    }

#if defined(CONFIG_SOLARYOU_RUNTIME_FULL_CONTROL) && CONFIG_SOLARYOU_RUNTIME_FULL_CONTROL
    log_sensor_init_result("BNO085 IMU", sensor_status.imu_enabled,
                           sensor_status.imu_initialized, true);
    log_sensor_init_result("Four-light array", sensor_status.light_enabled,
                           sensor_status.light_initialized, true);
    log_sensor_init_result("INA219 power sensor", sensor_status.power_enabled,
                           sensor_status.power_initialized,
                           sensor_status.power_enabled);
    if (!all_selected_sensors_ready || !sensor_status.imu_enabled ||
        !sensor_status.imu_initialized || !sensor_status.light_enabled ||
        !sensor_status.light_initialized) {
        curr_state.mode = SYSTEM_ERROR;
        ESP_LOGE(TAG, "Required sensor initialization failed; see the report above");
        return;
    }
#else
    log_sensor_init_result("BNO085 IMU", sensor_status.imu_enabled,
                           sensor_status.imu_initialized, true);
    log_sensor_init_result("Four-light array", sensor_status.light_enabled,
                           sensor_status.light_initialized, false);
    log_sensor_init_result("INA219 power sensor", sensor_status.power_enabled,
                           sensor_status.power_initialized, false);
    if (!sensor_status.imu_enabled || !sensor_status.imu_initialized) {
        curr_state.mode = SYSTEM_ERROR;
        ESP_LOGE(TAG, "BNO085 IMU is required for pitch/roll UART testing");
        return;
    }
    if (!all_selected_sensors_ready) {
        ESP_LOGW(TAG, "An optional sensor is unavailable; continuing with IMU UART testing");
    }
#endif

    // calibrate IMU (50 samples in sensor_service/include/component_config.h)
    if(!sensor_adapter_capture_imu_neutral()){
        curr_state.mode = SYSTEM_ERROR;
        ESP_LOGE(TAG, "IMU Calibration Error");
        return;
    }
    ESP_LOGI(TAG, "BNO085 IMU calibration: PASS");

    // Full integration requires the light array. Sensor/dashboard mode reports
    // its status but keeps pitch/roll UART running if it is unavailable.
#if defined(CONFIG_SOLARYOU_RUNTIME_FULL_CONTROL) && CONFIG_SOLARYOU_RUNTIME_FULL_CONTROL
    if(!sensor_adapter_capture_light_neutral()){
        curr_state.mode = SYSTEM_ERROR;
        ESP_LOGE(TAG, "Light Sensor Calibration Error");
        return;
    }
    ESP_LOGI(TAG, "Four-light array calibration: PASS");
#else
    bool light_ready = false;
    if (sensor_status.light_enabled && sensor_status.light_initialized) {
        light_ready = sensor_adapter_capture_light_neutral();
        if (light_ready) {
            ESP_LOGI(TAG, "Four-light array calibration: PASS");
        } else {
            ESP_LOGW(TAG, "Four-light array calibration: FAIL (optional in this mode)");
        }
    } else {
        ESP_LOGW(TAG, "Four-light array calibration: SKIPPED (optional in this mode)");
    }
#endif

    esp_err_t err = ESP_OK;

#if defined(CONFIG_SOLARYOU_RUNTIME_FULL_CONTROL) && CONFIG_SOLARYOU_RUNTIME_FULL_CONTROL
    // Full integration mode can command the physical servos.
    err = stabilization_init(&roll_ctlr, &pitch_ctlr, &stab_config);
    if (err != ESP_OK){
        curr_state.mode = SYSTEM_ERROR;
        ESP_LOGE(TAG, "Stabilization Initialization failed: %s\n", esp_err_to_name(err));
        return;
    }
    ESP_LOGW(TAG, "FULL CONTROL MODE: motor outputs are enabled");
#else
    // Sensor/dashboard mode deliberately skips motor and PID initialization.
    ESP_LOGI(TAG, "SENSOR/DASHBOARD TEST MODE: motor outputs are disabled");
#endif

    curr_state.mode = SYSTEM_BASIC_CONTROL;
    int64_t curr_time;
    int64_t last_stabilize_time = esp_timer_get_time();
    int64_t last_light_time = esp_timer_get_time();
    int64_t last_telem_time = esp_timer_get_time();


    while(1){
        curr_time = esp_timer_get_time();

        if(curr_time - last_stabilize_time >= STAB_TASK_DT_US){
            last_stabilize_time = curr_time;

#if defined(CONFIG_SOLARYOU_RUNTIME_FULL_CONTROL) && CONFIG_SOLARYOU_RUNTIME_FULL_CONTROL
            stab_output_t stab_output = {0};
            err = stabilization_update(&roll_ctlr, &pitch_ctlr, &stab_output, &stab_config.task_dt, curr_state.roll_light, curr_state.pitch_light);

            if (err != ESP_OK){
                curr_state.mode = SYSTEM_ERROR;
                ESP_LOGE(TAG, "Stabilization task failed: %s\n", esp_err_to_name(err));

            } else {
                curr_state.roll_imu = stab_output.roll_imu;
                curr_state.pitch_imu = stab_output.pitch_imu;
                curr_state.roll_cmd = stab_output.roll_cmd;
                curr_state.pitch_cmd = stab_output.pitch_cmd;

            }
#else
            // Read the real IMU for UART telemetry without issuing servo commands.
            err = sensor_dashboard_update_imu(&curr_state);
            if (err != ESP_OK){
                curr_state.mode = SYSTEM_ERROR;
                ESP_LOGE(TAG, "IMU telemetry update failed: %s\n", esp_err_to_name(err));
            }
#endif

            curr_time = esp_timer_get_time();
        }

        if(curr_time - last_light_time >= LIGHT_TASK_DT_US){
            last_light_time = curr_time;

#if defined(CONFIG_SOLARYOU_RUNTIME_FULL_CONTROL) && CONFIG_SOLARYOU_RUNTIME_FULL_CONTROL
            err = light_update(&curr_state, &stab_config);
            if(err != ESP_OK){
                curr_state.mode = SYSTEM_ERROR;
                ESP_LOGE(TAG, "Light task failed: %s\n", esp_err_to_name(err));
            }
#else
            if (light_ready) {
                err = light_update(&curr_state, &stab_config);
                if (err != ESP_OK) {
                    light_ready = false;
                    ESP_LOGW(TAG, "Four-light array read failed; continuing IMU UART testing");
                }
            }
#endif

            curr_time = esp_timer_get_time();
        }

        if(curr_time - last_telem_time >= TELEM_TASK_DT_US){
            last_telem_time = curr_time;

            // Send one newline-terminated JSON object for Zack's dashboard.
            // Power remains null until the INA219 value is connected to curr_state.
            uart_telemetry_send(&curr_state);
        }
    
        vTaskDelay(1);
    }
}
