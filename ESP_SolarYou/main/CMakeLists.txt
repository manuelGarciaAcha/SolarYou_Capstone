idf_component_register(SRCS "ESP_SolarYou.c" "stabilization_task.c"
                    INCLUDE_DIRS "."
                    REQUIRES
                        esp_timer
                        sensor_service
                        pid_stabilization
                        motor_control
                        system_state
                        board_config
                    )

target_compile_options(
    ${COMPONENT_LIB} 
    PRIVATE 
    -Wall 
    -Wextra 
    -Wconversion 
    -Wdouble-promotion
    )

