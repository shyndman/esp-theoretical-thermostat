#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t wifi_remote_manager_start(void);
bool wifi_remote_manager_is_ready(void);

#ifdef __cplusplus
}
#endif
