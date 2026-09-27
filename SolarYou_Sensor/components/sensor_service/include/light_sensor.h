#ifndef LIGHT_SENSOR_H
#define LIGHT_SENSOR_H

#include <stddef.h>
#include "sensor_status.h"
#include "sensor_types.h"

void lightSensorBegin();
bool lightSensorCaptureNeutral(size_t sampleCount);
bool lightSensorReadCentered(LightReading &reading);
void lightSensorGetHealth(sensor_health_t &health);
void lightSensorClearLatchedFaults();

#endif

