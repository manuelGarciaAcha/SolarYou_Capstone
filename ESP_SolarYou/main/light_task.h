#ifndef LIGHT_TASK_H_INCLUDED
#define LIGHT_TASK_H_INCLUDED

#include "esp_err.h"
esp_err_t light_update(system_state_t *state, const stab_config_t *config, float dt);

#endif