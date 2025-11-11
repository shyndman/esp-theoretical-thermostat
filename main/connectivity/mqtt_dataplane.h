#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t mqtt_dataplane_start(void);
esp_err_t mqtt_dataplane_publish_temperature_command(float cooling_setpoint_c,
                                                     float heating_setpoint_c);

#ifdef __cplusplus
}
#endif
