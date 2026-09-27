#include "sensor_math.h"

#include <cmath>

static float clampValue(float value, float minimum, float maximum)
{
    if (value < minimum) return minimum;
    if (value > maximum) return maximum;
    return value;
}

float centerSensorValue(float rawValue, float neutralValue, float sign,
                        float deadband, float minimum, float maximum)
{
    const float centered = (rawValue - neutralValue) * sign;
    if (std::fabs(centered) <= deadband) return 0.0f;
    return clampValue(centered, minimum, maximum);
}

sensor_fault_flags_t evaluateLightReading(const LightRawReading &raw,
                                          int minimumTotalCount,
                                          int saturationCount,
                                          int openCount,
                                          int shortCount,
                                          float &pitchImbalance,
                                          float &rollImbalance)
{
    const int top = raw.topLeft + raw.topRight;
    const int bottom = raw.bottomLeft + raw.bottomRight;
    const int left = raw.topLeft + raw.bottomLeft;
    const int right = raw.topRight + raw.bottomRight;
    const int total = top + bottom;
    pitchImbalance = total > 0 ? static_cast<float>(top - bottom) / total : 0.0f;
    rollImbalance = total > 0 ? static_cast<float>(left - right) / total : 0.0f;

    sensor_fault_flags_t faults = SENSOR_FAULT_NONE;
    if (total < minimumTotalCount) faults |= SENSOR_FAULT_TOO_DARK;
    if (raw.topLeft > saturationCount || raw.topRight > saturationCount ||
        raw.bottomLeft > saturationCount || raw.bottomRight > saturationCount) {
        faults |= SENSOR_FAULT_SATURATED;
    }
    if (total >= minimumTotalCount &&
        (raw.topLeft <= openCount || raw.topRight <= openCount ||
         raw.bottomLeft <= openCount || raw.bottomRight <= openCount)) {
        faults |= SENSOR_FAULT_CHANNEL_OPEN;
    }
    if (raw.topLeft >= shortCount || raw.topRight >= shortCount ||
        raw.bottomLeft >= shortCount || raw.bottomRight >= shortCount) {
        faults |= SENSOR_FAULT_CHANNEL_SHORT;
    }
    return faults;
}

bool calculateElectricalPower(float voltageV, float currentA, float &powerW)
{
    if (!std::isfinite(voltageV) || !std::isfinite(currentA)) {
        powerW = 0.0f;
        return false;
    }
    powerW = voltageV * currentA;
    return std::isfinite(powerW);
}
