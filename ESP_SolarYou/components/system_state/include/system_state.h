#ifndef SYSTEM_STATE_H_INCLUDED
#define SYSTEM_STATE_H_INCLUDED

#include <stdint.h>

#define STAB_TASK_DT_US         20000      // microseconds
#define TELEM_TASK_DT_US        200000
#define LIGHT_TASK_DT_US        500000

typedef enum {

    SYSTEM_RESET,
    SYSTEM_BASIC_CONTROL,
    SYSTEM_ERROR

} system_mode_t;

// typedef enum {

//     TASK_TELEMETRY,
//     TASK_STABILIZATION,
//     TASK_LIGHT,
//     TASK_NONE

// } system_task_t;

typedef struct {

    // tracking and stabilization
    float roll_imu;         // last measured roll angle
    float pitch_imu;        // last measured pitch angle
    float roll_light;       // last measured target sun angle (roll)
    float pitch_light;      // "            ...             " (pitch)
    float roll_cmd;       // last correction command to roll servo
    float pitch_cmd;      // "         ...            " pitch servo
    
    // power
    float voltage;
    float current;
    float power;
    float avg_power;
    uint32_t power_samples;

    // system
    system_mode_t mode;
    //system_task_t task;

} system_state_t;

void sys_state_reset (system_state_t *state);

#endif