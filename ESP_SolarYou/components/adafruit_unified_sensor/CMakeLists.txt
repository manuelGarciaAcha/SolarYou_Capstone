if(NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/vendor/Adafruit_Sensor.cpp")
    message(FATAL_ERROR
        "Adafruit INA219 sources are missing. Run (Windows) scripts/fetch_arduino_libraries.ps1 or (Linux) scripts/fetch_arduino_libs.sh first.")
endif()

idf_component_register(
    SRCS "vendor/Adafruit_Sensor.cpp"
    INCLUDE_DIRS "vendor"
    REQUIRES arduino-esp32
)

