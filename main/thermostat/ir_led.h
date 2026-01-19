#pragma once

#include <stdbool.h>

#include "esp_err.h"

esp_err_t thermostat_ir_led_init(void);
void thermostat_ir_led_set(bool on);
