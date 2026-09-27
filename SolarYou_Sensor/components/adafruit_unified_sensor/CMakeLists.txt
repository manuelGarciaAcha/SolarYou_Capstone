if(NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/vendor/Adafruit_Sensor.cpp")
    message(FATAL_ERROR
        "Adafruit Unified Sensor sources are missing. Run scripts/fetch_arduino_libraries.ps1 first.")
endif()

idf_component_register(
    SRCS "vendor/Adafruit_Sensor.cpp"
    INCLUDE_DIRS "vendor"
    REQUIRES arduino-esp32
)

