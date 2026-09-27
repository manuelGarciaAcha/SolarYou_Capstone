if(NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/vendor/Adafruit_INA219.cpp")
    message(FATAL_ERROR
        "Adafruit INA219 sources are missing. Run scripts/fetch_arduino_libraries.ps1 first.")
endif()

idf_component_register(
    SRCS "vendor/Adafruit_INA219.cpp"
    INCLUDE_DIRS "vendor"
    REQUIRES arduino-esp32 adafruit_busio
)

