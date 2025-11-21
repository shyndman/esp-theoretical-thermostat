#pragma once

#include <stdbool.h>

#include "esp_err.h"

esp_err_t thermostat_led_status_init(void);
void thermostat_led_status_booting(void);
void thermostat_led_status_boot_complete(void);
void thermostat_led_status_set_hvac(bool heating, bool cooling);
