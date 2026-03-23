#ifndef MQTT_LOG_MIRROR_H
#define MQTT_LOG_MIRROR_H

#include "esp_err.h"

/**
 * @brief Start the MQTT log mirror.
 *
 * This installs a custom esp_log_set_vprintf sink that forwards logs to
 * the shared MQTT client. It should be called after mqtt_manager_start().
 *
 * @return ESP_OK on success, or an error code on failure.
 */
esp_err_t mqtt_log_mirror_start(void);

#endif // MQTT_LOG_MIRROR_H
