set(sensor_service_sources
    "sensor_api.cpp"
    "sensor_json.cpp"
    "sensor_status.cpp"
    "sensor_math.cpp"
)

if(CONFIG_SOLARYOU_VALIDATION_ALL OR CONFIG_SOLARYOU_VALIDATION_LIGHT_ONLY)
    list(APPEND sensor_service_sources "light_sensor.cpp")
endif()

if(CONFIG_SOLARYOU_SIMULATION)
    if(CONFIG_SOLARYOU_VALIDATION_ALL OR CONFIG_SOLARYOU_VALIDATION_IMU_ONLY)
        list(APPEND sensor_service_sources "simulation/imu_sensor_sim.cpp")
    endif()
    if(CONFIG_SOLARYOU_VALIDATION_ALL OR CONFIG_SOLARYOU_VALIDATION_POWER_ONLY)
        list(APPEND sensor_service_sources "simulation/power_sensor_sim.cpp")
    endif()
else()
    if(CONFIG_SOLARYOU_VALIDATION_ALL OR CONFIG_SOLARYOU_VALIDATION_IMU_ONLY OR
       CONFIG_SOLARYOU_VALIDATION_POWER_ONLY)
        list(APPEND sensor_service_sources "sensor_bus.cpp")
    endif()
    if(CONFIG_SOLARYOU_VALIDATION_ALL OR CONFIG_SOLARYOU_VALIDATION_IMU_ONLY)
        list(APPEND sensor_service_sources "imu_sensor.cpp")
    endif()
    if(CONFIG_SOLARYOU_VALIDATION_ALL OR CONFIG_SOLARYOU_VALIDATION_POWER_ONLY)
        list(APPEND sensor_service_sources "power_sensor.cpp")
    endif()
endif()

idf_component_register(
    SRCS ${sensor_service_sources}
    INCLUDE_DIRS "include"
    REQUIRES arduino-esp32 json adafruit_bno08x adafruit_ina219
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_11)
