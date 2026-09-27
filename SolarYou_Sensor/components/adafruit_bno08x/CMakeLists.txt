if(NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/vendor/src/Adafruit_BNO08x.cpp")
    message(FATAL_ERROR
        "Adafruit BNO08x sources are missing. Run scripts/fetch_arduino_libraries.ps1 first.")
endif()

idf_component_register(
    SRCS
        "vendor/src/Adafruit_BNO08x.cpp"
        "vendor/src/sh2.c"
        "vendor/src/sh2_SensorValue.c"
        "vendor/src/sh2_util.c"
        "vendor/src/shtp.c"
    INCLUDE_DIRS "vendor/src"
    REQUIRES arduino-esp32 adafruit_busio adafruit_unified_sensor
)

