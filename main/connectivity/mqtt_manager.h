#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "mqtt_client.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t mqtt_manager_start(void);
bool mqtt_manager_is_ready(void);
const char *mqtt_manager_uri(void);
esp_mqtt_client_handle_t mqtt_manager_get_client(void);

#ifdef __cplusplus
}
#endif
