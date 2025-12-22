#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

enum { WIFI_REMOTE_MANAGER_IPV4_STR_LEN = 16 };

esp_err_t wifi_remote_manager_start(void);
bool wifi_remote_manager_is_ready(void);
esp_err_t wifi_remote_manager_get_sta_ip(char *buffer, size_t buffer_len);

#ifdef __cplusplus
}
#endif
