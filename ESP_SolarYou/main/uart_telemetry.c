#include "uart_telemetry.h"

#include <stdio.h>

void uart_telemetry_send(const system_state_t *state)
{
    if (state == NULL) {
        return;
    }

    // Sends the latest readings as JSON for Zack's dashboard.
    printf(
        "{\"pitch\":%.2f,\"roll\":%.2f,\"power\":null}\n",
        (double)state->pitch_imu,
        (double)state->roll_imu
    );
    fflush(stdout);
}
