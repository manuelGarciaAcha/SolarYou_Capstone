#include "ble_app.h"
#include "ble_telemetry.h"
#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <math.h>

/* Reuse application readings and publish a snapshot at up to 10 Hz. */
#define BLE_CAPTURE_INTERVAL_US 100000
#define BLE_READING_MAX_AGE_US 1000000
static sy_telemetry_sample_t snapshot;
static int64_t last_capture_us;
static int64_t last_imu_update_us;
static int64_t last_targets_update_us;
static int64_t last_power_update_us;
static bool started;

void ble_app_start(void)
{
    esp_err_t result = ble_telemetry_init();
    if (result != ESP_OK) {
        ESP_LOGE("BLE", "BLE unavailable: %s", esp_err_to_name(result));
        return;
    }
    started = true;
    snapshot.captured_ms = (uint32_t)(esp_timer_get_time() / 1000);
    ble_telemetry_publish(&snapshot);
}

void ble_app_update_imu(float pitch, float roll, bool valid)
{
    snapshot.pitch = pitch;
    snapshot.roll = roll;
    snapshot.imu_valid = valid && isfinite(pitch) && isfinite(roll);
    last_imu_update_us = esp_timer_get_time();
}

void ble_app_update_targets(float pitch, float roll, bool valid)
{
#if defined(CONFIG_SOLARYOU_RUNTIME_FULL_CONTROL) && CONFIG_SOLARYOU_RUNTIME_FULL_CONTROL
    /* These are the same setpoints already passed to the existing controller. */
    sy_telemetry_set_targets(&snapshot, pitch, roll, valid);
#else
    (void)pitch; (void)roll; (void)valid;
    snapshot.targets_valid = false;
#endif
    last_targets_update_us = esp_timer_get_time();
}

void ble_app_update_power(float voltage, float current, float watts,
                          float avg_watts, bool valid)
{
    snapshot.voltage = voltage;
    snapshot.current = current;
    snapshot.power = watts;
    snapshot.avg_power = avg_watts;
    snapshot.power_valid = valid && isfinite(voltage) && isfinite(current) &&
        isfinite(watts) && isfinite(avg_watts);
    last_power_update_us = esp_timer_get_time();
}

void ble_app_poll(void)
{
    if (!started) return;
    int64_t now = esp_timer_get_time();
    if (now - last_capture_us < BLE_CAPTURE_INTERVAL_US) return;
    /* Reading age is measured from acquisition, not publication. */
    if (now - last_imu_update_us > BLE_READING_MAX_AGE_US) snapshot.imu_valid = false;
    if (now - last_targets_update_us > BLE_READING_MAX_AGE_US) snapshot.targets_valid = false;
    if (now - last_power_update_us > BLE_READING_MAX_AGE_US) snapshot.power_valid = false;
    last_capture_us = now;
    snapshot.captured_ms = (uint32_t)(now / 1000);
    ble_telemetry_publish(&snapshot);
}
