#ifndef POWER_N_TELEMETRY_H_INCLUDED
#define POWER_N_TELEMETRY_H_INCLUDED

#include "system_state.h"
#include "esp_err.h"

/**
 * Sends one set of readings to the dashboard as JSON.
 * Each message ends with a newline so the dashboard knows
 * when it has received the complete reading.
 *
 * Example message:
 * {"pitch":1.23,"roll":-0.50,"power":null}
 */
void uart_telemetry_send(const system_state_t *state);
esp_err_t power_update(system_state_t *state);


#endif
