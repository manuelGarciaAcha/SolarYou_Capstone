#include "system_state.h"

void sys_state_reset (system_state_t *state)
{
    state->roll_imu = 0;
    state->pitch_imu = 0;
    state->roll_light = 0;
    state->pitch_light = 0;
    state->roll_cmd = 0;
    state->pitch_cmd = 0;
    state->voltage = 0;
    state->current = 0;
    state->power = 0;
    state->avg_power = 0;
    state->power_samples = 0;
    state->mode = SYSTEM_RESET;
    // state->task = TASK_NONE;
}