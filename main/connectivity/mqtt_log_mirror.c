#include "mqtt_log_mirror.h"
#include "esp_log.h"
#include "mqtt_client.h"
#include "connectivity/mqtt_manager.h"
#include "connectivity/device_identity.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static vprintf_like_t s_original_sink = NULL;
static char s_log_topic[160];

static void log_warning_to_original_sink(const char *fmt, ...)
{
    if (s_original_sink == NULL) {
        return;
    }
    va_list args;
    va_start(args, fmt);
    s_original_sink(fmt, args);
    va_end(args);
}

static int mqtt_log_mirror_sink(const char *fmt, va_list args)
{
    // 1. Forward to original sink (UART) first
    // We must copy va_list because it's consumed by the call
    va_list args_local;
    va_copy(args_local, args);
    int ret = s_original_sink(fmt, args_local);
    va_end(args_local);

    // 2. Best-effort mirror to MQTT
    if (!mqtt_manager_is_ready()) {
        return ret;
    }

    esp_mqtt_client_handle_t client = mqtt_manager_get_client();
    if (client == NULL) {
        return ret;
    }

    // Format the line for MQTT. Re-entrant safe per design.
    // We use a stack buffer to avoid global locks or shared mutable state.
    char line[256];
    va_copy(args_local, args);
    int formatted_len = vsnprintf(line, sizeof(line), fmt, args_local);
    va_end(args_local);

    if (formatted_len <= 0) {
        return ret;
    }

    // Enqueue to MQTT: QoS 0, retain=false, store=true
    // QoS 0 with store=true allows the message to be queued in the outbox.
    int res = esp_mqtt_client_enqueue(client, s_log_topic, line, 0, 0, false, true);
    if (res < 0) {
        // On failure, we only log through the preserved original sink to avoid recursion.
        log_warning_to_original_sink("W (mqtt_log_mirror) remote log enqueue failed\n");
    }

    return ret;
}

esp_err_t mqtt_log_mirror_start(void)
{
    if (s_original_sink != NULL) {
        return ESP_OK;
    }

    const char *device_root = device_identity_get_theo_device_topic_root();
    if (device_root == NULL || device_root[0] == '\0') {
        return ESP_ERR_INVALID_STATE;
    }

    // Build the cached log topic from the canonical Theo device root.
    snprintf(s_log_topic, sizeof(s_log_topic), "%s/logs", device_root);

    // Install the mirror sink
    s_original_sink = esp_log_set_vprintf(mqtt_log_mirror_sink);
    
    return ESP_OK;
}
