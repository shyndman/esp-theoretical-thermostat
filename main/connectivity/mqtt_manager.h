#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t mqtt_manager_start(void);
bool mqtt_manager_is_ready(void);
const char *mqtt_manager_uri(void);

#ifdef __cplusplus
}
#endif
