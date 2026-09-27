#include "sensor_api.h"
#include "sensor_json.h"

static void consume_snapshot(const sensor_snapshot_t *snapshot)
{
    if (snapshot != 0 && snapshot->imu.health.state == SENSOR_STATE_VALID) {
        (void)snapshot->imu.pitch_deg;
    }
}

int main(void)
{
    sensor_imu_sample_t imu = {0};
    sensor_light_sample_t light = {0};
    sensor_power_sample_t power = {0};
    sensor_snapshot_t snapshot = {0};
    char json[SENSOR_JSON_RECOMMENDED_CAPACITY] = {0};
    size_t json_length = 0U;

    (void)sensor_adapter_capture_imu_neutral();
    (void)sensor_adapter_capture_light_neutral();
    (void)sensor_adapter_read_imu(&imu);
    (void)sensor_adapter_read_light(&light);
    (void)sensor_adapter_read_power(&power);
    consume_snapshot(&snapshot);
    (void)sensor_snapshot_to_json(&snapshot, json, sizeof(json), &json_length);
    return 0;
}
