#ifndef SENSOR_MATH_H
#define SENSOR_MATH_H

#include "sensor_status.h"
#include "sensor_types.h"

float centerSensorValue(float rawValue,
                        float neutralValue,
                        float sign,
                        float deadband,
                        float minimum,
                        float maximum);

sensor_fault_flags_t evaluateLightReading(const LightRawReading &raw,
                                          int minimumTotalCount,
                                          int saturationCount,
                                          int openCount,
                                          int shortCount,
                                          float &pitchImbalance,
                                          float &rollImbalance);

bool calculateElectricalPower(float voltageV,
                              float currentA,
                              float &powerW);

#endif
