set(app_sources "power_n_telemetry.c" "ESP_SolarYou.c" "stabilization_task.c" "light_task.c")
# ESP-IDF discovers REQUIRES before CONFIG values are available. Keep SDK
# dependencies unconditional; the BLE sources/settings still control the radio.
set(app_dependencies esp_timer sensor_service pid_stabilization motor_control system_state board_config bt nvs_flash)

if(CONFIG_SOLARYOU_BLE_TELEMETRY)
    list(APPEND app_sources "ble_protocol.c" "ble_telemetry.c" "ble_app.c")
endif()

idf_component_register(SRCS ${app_sources}
                       INCLUDE_DIRS "."
                       REQUIRES ${app_dependencies})

target_compile_options(
    ${COMPONENT_LIB} 
    PRIVATE 
    -Wall 
    -Wextra 
    -Wconversion 
    -Wdouble-promotion
    )

