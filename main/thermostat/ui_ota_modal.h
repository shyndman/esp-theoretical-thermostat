#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t thermostat_ota_modal_show(size_t total_bytes);
esp_err_t thermostat_ota_modal_update(size_t written_bytes, size_t total_bytes);
esp_err_t thermostat_ota_modal_show_error(const char *message);
void thermostat_ota_modal_hide(void);
bool thermostat_ota_modal_is_visible(void);

#ifdef __cplusplus
}
#endif
