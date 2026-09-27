#include <Arduino.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>

#include "esp_rom_sys.h"
#include "sdkconfig.h"
#include "sensor_api.h"
#include "sensor_json.h"

#if CONFIG_SOLARYOU_SIMULATION
static void consolePrintf(const char *format, ...)
{
    char buffer[512];
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(buffer, sizeof(buffer), format, arguments);
    va_end(arguments);
    esp_rom_printf("%s", buffer);
}
#else
#define consolePrintf printf
#endif

#if SY_CONSOLE_PRINT_JSON
static void printJsonLine(const char *json)
{
#if CONFIG_SOLARYOU_SIMULATION
 // In Wokwi, this prints the JSON to the serial monitor so we can check it. Our usual print helper uses a 512-byte buffer and might cut it off, so we print it here
// instead. This does not send data to the dashboard
    esp_rom_printf("sensor_json,%s\n", json);
#else
    printf("sensor_json,%s\n", json);
#endif
}
#endif

static void printSensorLine(const char *name,
                            const sensor_health_t &health,
                            bool valid)
{
    consolePrintf("%s: valid=%d state=%s active=0x%08" PRIx32
                  " latched=0x%08" PRIx32 " age_ms=%" PRIu32 "\n",
                  name,
                  valid ? 1 : 0,
                  sensorStateName(health.state),
                  health.active_faults,
                  health.latched_faults,
                  health.age_ms);
}

static bool statusChanged(const sensor_snapshot_t &current,
                          const sensor_snapshot_t &previous,
                          bool havePrevious)
{
    if (!havePrevious || current.valid != previous.valid) return true;
    bool changed = false;
#if CONFIG_SOLARYOU_VALIDATION_ALL || CONFIG_SOLARYOU_VALIDATION_IMU_ONLY
    changed = changed || current.imu.valid != previous.imu.valid ||
        current.imu.health.state != previous.imu.health.state ||
        current.imu.health.active_faults != previous.imu.health.active_faults;
#endif
#if CONFIG_SOLARYOU_VALIDATION_ALL || CONFIG_SOLARYOU_VALIDATION_LIGHT_ONLY
    changed = changed || current.light.valid != previous.light.valid ||
        current.light.health.state != previous.light.health.state ||
        current.light.health.active_faults != previous.light.health.active_faults;
#endif
#if CONFIG_SOLARYOU_VALIDATION_ALL || CONFIG_SOLARYOU_VALIDATION_POWER_ONLY
    changed = changed || current.power.valid != previous.power.valid ||
        current.power.health.state != previous.power.health.state ||
        current.power.health.active_faults != previous.power.health.active_faults;
#endif
    return changed;
}

static void printSnapshot(const sensor_snapshot_t &snapshot)
{
    consolePrintf("\nsample: seq=%" PRIu32 " time_ms=%" PRIu32
                  " selected_valid=%d\n",
                  snapshot.sequence,
                  snapshot.device_uptime_ms,
                  snapshot.valid ? 1 : 0);
#if CONFIG_SOLARYOU_VALIDATION_ALL || CONFIG_SOLARYOU_VALIDATION_IMU_ONLY
    consolePrintf("  imu_values: pitch_deg=%.2f roll_deg=%.2f\n",
                  snapshot.imu.pitch_deg,
                  snapshot.imu.roll_deg);
    printSensorLine("imu", snapshot.imu.health, snapshot.imu.valid);
#endif
#if CONFIG_SOLARYOU_VALIDATION_ALL || CONFIG_SOLARYOU_VALIDATION_LIGHT_ONLY
    consolePrintf("  light_values: pitch_error=%.4f roll_error=%.4f"
                  " raw=%u/%u/%u/%u\n",
                  snapshot.light.pitch_error,
                  snapshot.light.roll_error,
                  snapshot.light.top_left_raw,
                  snapshot.light.top_right_raw,
                  snapshot.light.bottom_left_raw,
                  snapshot.light.bottom_right_raw);
    printSensorLine("light", snapshot.light.health, snapshot.light.valid);
#endif
#if CONFIG_SOLARYOU_VALIDATION_ALL || CONFIG_SOLARYOU_VALIDATION_POWER_ONLY
    consolePrintf("  power_values: voltage_v=%.3f current_a=%.3f power_w=%.3f"
                  " shunt_mv=%.3f\n",
                  snapshot.power.voltage_v,
                  snapshot.power.current_a,
                  snapshot.power.power_w,
                  snapshot.power.shunt_voltage_mv);
    printSensorLine("power", snapshot.power.health, snapshot.power.valid);
#endif

#if SY_CONSOLE_PRINT_JSON
    // Eventhough JSON is handy for debugging, it makes the test output hard to read. Leave it off for now, unless there's a need to check the full message.
    static char json[SENSOR_JSON_RECOMMENDED_CAPACITY];
    size_t jsonLength = 0U;
    const sensor_json_result_t jsonResult = sensor_snapshot_to_json(
        &snapshot, json, sizeof(json), &jsonLength);
    if (jsonResult == SENSOR_JSON_OK) {
        printJsonLine(json);
    } else {
        consolePrintf("sensor_json_error=%d required_buffer=%u\n",
                      static_cast<int>(jsonResult),
                      static_cast<unsigned int>(sizeof(json)));
    }
#endif
}

static const char *validationModeName()
{
#if CONFIG_SOLARYOU_VALIDATION_LIGHT_ONLY
    return "LIGHT ONLY";
#elif CONFIG_SOLARYOU_VALIDATION_IMU_ONLY
    return "IMU ONLY";
#elif CONFIG_SOLARYOU_VALIDATION_POWER_ONLY
    return "POWER ONLY";
#else
    return "ALL SENSORS";
#endif
}

extern "C" void app_main(void)
{
    consolePrintf("Starting Arduino compatibility layer...\n");
    initArduino();
    consolePrintf("Arduino compatibility layer ready.\n");
    delay(500);

    consolePrintf("SolarYou sensor adapter: ESP-IDF 5.5.5 + Arduino Core 3.3.11\n");
    consolePrintf("No custom RTOS sensor task is created by this validation app.\n");
#if CONFIG_SOLARYOU_SIMULATION
    consolePrintf("SIMULATION MODE: adjustable mock backends are active.\n");
    consolePrintf("VALIDATION MODE: %s\n", validationModeName());
#if CONFIG_SOLARYOU_VALIDATION_ALL
    consolePrintf("Inputs: light=GPIO3/4/5/6 imu=GPIO7/8 power=GPIO9/10 "
                  "fault_buttons=GPIO11/12\n");
#elif CONFIG_SOLARYOU_VALIDATION_LIGHT_ONLY
    consolePrintf("Inputs: light TL/TR/BL/BR=GPIO3/4/5/6\n");
#elif CONFIG_SOLARYOU_VALIDATION_IMU_ONLY
    consolePrintf("Inputs: IMU pitch/roll=GPIO7/8 timeout_button=GPIO11\n");
#elif CONFIG_SOLARYOU_VALIDATION_POWER_ONLY
    consolePrintf("Inputs: PV voltage/current=GPIO9/10 comm_button=GPIO12\n");
#endif
#else
    consolePrintf("PHYSICAL MODE: BNO085 and INA219 hardware drivers are active.\n");
    consolePrintf("VALIDATION MODE: %s\n", validationModeName());
    consolePrintf("Target: Heltec WiFi LoRa 32 V4.3 / ESP32-S3R2 / 16MB flash.\n");
    consolePrintf("PSRAM: physically present (2MB Quad), intentionally disabled.\n");
#endif

    const bool initialized = sensor_adapter_init();
    const bool calibrated = sensor_adapter_capture_neutral();
    consolePrintf("sensor_init=%d sensor_calibration=%d\n",
                  initialized ? 1 : 0,
                  calibrated ? 1 : 0);
    consolePrintf("Sampling every %u ms; reporting every %u ms or when status changes.\n",
                  static_cast<unsigned int>(SY_SENSOR_POLL_PERIOD_MS),
                  static_cast<unsigned int>(SY_CONSOLE_REPORT_PERIOD_MS));
    consolePrintf("Full JSON terminal output is %s.\n",
                  SY_CONSOLE_PRINT_JSON ? "enabled" : "disabled");

    uint32_t lastReportMs = 0U;
    bool havePreviousStatus = false;
    sensor_snapshot_t previousStatus = {};

    while (true) {
        sensor_snapshot_t snapshot = {};
        sensor_adapter_read(&snapshot);

        const uint32_t now = millis();
        const bool reportDue = !havePreviousStatus ||
            (now - lastReportMs) >= SY_CONSOLE_REPORT_PERIOD_MS;
        if (reportDue || statusChanged(snapshot, previousStatus,
                                       havePreviousStatus)) {
            printSnapshot(snapshot);
            lastReportMs = now;
        }

        // Saves the last result so we can spot status changes. The sensors still get checked every 100 ms though even when nothing is printed.
        previousStatus = snapshot;
        havePreviousStatus = true;
        delay(SY_SENSOR_POLL_PERIOD_MS);
    }
}
