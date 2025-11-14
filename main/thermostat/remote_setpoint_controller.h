#pragma once

#include "thermostat/ui_state.h"

#ifdef __cplusplus
extern "C" {
#endif

void thermostat_remote_setpoint_controller_init(void);
void thermostat_remote_setpoint_controller_submit(thermostat_target_t target, float value_c);

#ifdef __cplusplus
}
#endif
