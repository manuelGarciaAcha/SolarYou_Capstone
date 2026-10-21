#ifndef IMU_SENSOR_H
#define IMU_SENSOR_H

#include <stddef.h>
#include "sensor_status.h"
#include "sensor_types.h"

bool imuSensorBegin();
bool imuSensorCaptureNeutral(size_t sampleCount);
bool imuSensorReadCentered(AxisReading &reading);
void imuSensorGetHealth(sensor_health_t &health);
void imuSensorClearLatchedFaults();

#endif

