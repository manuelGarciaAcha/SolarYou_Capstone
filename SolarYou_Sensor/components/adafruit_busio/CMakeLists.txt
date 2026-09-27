if(NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/vendor/Adafruit_I2CDevice.cpp")
    message(FATAL_ERROR
        "Adafruit BusIO sources are missing. Run scripts/fetch_arduino_libraries.ps1 first.")
endif()

idf_component_register(
    SRCS
        "vendor/Adafruit_BusIO_Register.cpp"
        "vendor/Adafruit_GenericDevice.cpp"
        "vendor/Adafruit_I2CDevice.cpp"
        "vendor/Adafruit_SPIDevice.cpp"
    INCLUDE_DIRS "vendor"
    REQUIRES arduino-esp32
)

