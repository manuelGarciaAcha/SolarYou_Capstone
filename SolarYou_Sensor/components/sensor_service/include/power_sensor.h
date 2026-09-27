#ifndef POWER_SENSOR_H
#define POWER_SENSOR_H

#include "sensor_status.h"
#include "sensor_types.h"

bool powerSensorBegin();
bool powerSensorRead(PowerReading &reading);
void powerSensorGetHealth(sensor_health_t &health);
void powerSensorClearLatchedFaults();

#endif

