#pragma once

#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*mqtt_dataplane_status_cb_t)(const char *status, void *ctx);

esp_err_t mqtt_dataplane_start(mqtt_dataplane_status_cb_t status_cb, void *ctx);
esp_err_t mqtt_dataplane_publish_temperature_command(float cooling_setpoint_c,
                                                     float heating_setpoint_c);

/**
 * Wait for essential initial state to be received via MQTT.
 *
 * Blocks until weather, room, and HVAC data have all been received,
 * or until timeout_ms elapses. Calls status_cb with progress updates
 * as each piece of data arrives.
 *
 * @param status_cb  Callback invoked with status messages (may be NULL)
 * @param ctx        Context passed to status_cb
 * @param timeout_ms Maximum time to wait in milliseconds
 * @return ESP_OK if all data received, ESP_ERR_TIMEOUT on timeout
 */
esp_err_t mqtt_dataplane_await_initial_state(mqtt_dataplane_status_cb_t status_cb,
                                             void *ctx,
                                             uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif
