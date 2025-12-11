#pragma once

#include <stdbool.h>

#include "esp_err.h"

esp_err_t thermostat_led_status_init(void);
void thermostat_led_status_booting(void);
void thermostat_led_status_boot_complete(void);
void thermostat_led_status_set_hvac(bool heating, bool cooling);
void thermostat_led_status_trigger_rainbow(void);
void thermostat_led_status_trigger_heatwave(void);
void thermostat_led_status_trigger_coolwave(void);
void thermostat_led_status_trigger_sparkle(void);
