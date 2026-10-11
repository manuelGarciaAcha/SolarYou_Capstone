#ifndef SENSOR_TYPES_H
#define SENSOR_TYPES_H

#include <stdint.h>

struct AxisReading {
    float pitch;
    float roll;
    bool valid;
};

struct ImuRawReading {
    float yaw_deg;
    float pitch_deg;
    float roll_deg;
};

struct LightRawReading {
    uint16_t topLeft;
    uint16_t topRight;
    uint16_t bottomLeft;
    uint16_t bottomRight;
};

struct LightReading {
    LightRawReading raw;
    float pitch;
    float roll;
    bool valid;
};

struct PowerReading {
    float busVoltageV;
    float shuntVoltageMv;
    float currentA;
    float powerW;
    bool valid;
};

#endif

