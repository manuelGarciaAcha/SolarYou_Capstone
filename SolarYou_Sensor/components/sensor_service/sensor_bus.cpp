#include "sensor_bus.h"

#include <Arduino.h>
#include <Wire.h>
#include "component_config.h"

static bool busReady = false;

bool sensorBusBegin()
{
    if (!busReady) {
        busReady = Wire.begin(SENSOR_I2C_SDA_GPIO, SENSOR_I2C_SCL_GPIO);
    }
    return busReady;
}

bool sensorBusIsReady()
{
    return busReady;
}

