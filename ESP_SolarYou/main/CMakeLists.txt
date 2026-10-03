idf_component_register(SRCS "wifi_config.c" "telemetry_task.c" "ESP_SolarYou.c" "PID_stabilization.c" "motor_control.c" "stabilization_task.c" "system_state.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp_http_server esp_wifi esp_timer
                    )
