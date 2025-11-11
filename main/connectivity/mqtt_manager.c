#include "connectivity/mqtt_manager.h"

#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"
#include "mqtt_client.h"
#include "sdkconfig.h"

static const char *TAG = "mqtt";
static esp_mqtt_client_handle_t s_client;
static bool s_client_started;
static bool s_connected;
static char s_broker_uri[160];

static void log_error_details(const esp_mqtt_error_codes_t *err)
{
    if (err == NULL) {
        return;
    }
    ESP_LOGE(TAG,
             "MQTT error type=%d tls_err=0x%x stack_err=0x%x sock_errno=%d return_code=%d",
             err->error_type,
             err->esp_tls_last_esp_err,
             err->esp_tls_stack_err,
             err->esp_transport_sock_errno,
             err->connect_return_code);
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;
    switch (event_id) {
    case MQTT_EVENT_CONNECTED:
        s_connected = true;
        ESP_LOGI(TAG, "[%s] CONNECTED (session=%d)", s_broker_uri, event->session_present);
        break;
    case MQTT_EVENT_DISCONNECTED:
        s_connected = false;
        ESP_LOGW(TAG, "[%s] DISCONNECTED", s_broker_uri);
        break;
    case MQTT_EVENT_ERROR:
        s_connected = false;
        ESP_LOGE(TAG, "[%s] ERROR event", s_broker_uri);
        log_error_details(event ? event->error_handle : NULL);
        break;
    default:
        break;
    }
}

static esp_err_t build_broker_uri(void)
{
    const char *host = CONFIG_THEO_MQTT_HOST;
    const char *path_cfg = CONFIG_THEO_MQTT_PATH;
    const bool path_present = (path_cfg != NULL && path_cfg[0] != '\0');
    ESP_RETURN_ON_FALSE(host != NULL && host[0] != '\0', ESP_ERR_INVALID_STATE, TAG,
                        "CONFIG_THEO_MQTT_HOST must be set");
    const char *path_sep = (path_present && path_cfg[0] != '/') ? "/" : "";
    int written = snprintf(s_broker_uri,
                           sizeof(s_broker_uri),
                           "ws://%s:%d%s%s",
                           host,
                           CONFIG_THEO_MQTT_PORT,
                           path_present ? path_sep : "",
                           path_present ? path_cfg : "");
    ESP_RETURN_ON_FALSE(written > 0 && written < (int)sizeof(s_broker_uri), ESP_ERR_INVALID_SIZE, TAG,
                        "Broker URI overflow");
    ESP_LOGI(TAG, "MQTT URI %s (heap=%u, min_heap=%u)", s_broker_uri, esp_get_free_heap_size(), esp_get_minimum_free_heap_size());
    return ESP_OK;
}

esp_err_t mqtt_manager_start(void)
{
    if (s_client_started) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(build_broker_uri(), TAG, "build URI failed");

    esp_mqtt_client_config_t cfg = {
        .broker.address.uri = s_broker_uri,
    };

    s_client = esp_mqtt_client_init(&cfg);
    if (s_client == NULL) {
        ESP_LOGE(TAG, "[%s] esp_mqtt_client_init returned NULL (heap=%u, min_heap=%u)",
                 s_broker_uri, esp_get_free_heap_size(), esp_get_minimum_free_heap_size());
        return ESP_ERR_NO_MEM;
    }

    ESP_RETURN_ON_ERROR(esp_mqtt_client_register_event(s_client, MQTT_EVENT_ANY, mqtt_event_handler, NULL), TAG,
                        "register event failed");

    esp_err_t err = esp_mqtt_client_start(s_client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "[%s] esp_mqtt_client_start failed (%s)", s_broker_uri, esp_err_to_name(err));
        esp_mqtt_client_destroy(s_client);
        s_client = NULL;
        return err;
    }

    s_client_started = true;
    ESP_LOGI(TAG, "[%s] MQTT client starting", s_broker_uri);
    return ESP_OK;
}

bool mqtt_manager_is_ready(void)
{
    return s_connected;
}

const char *mqtt_manager_uri(void)
{
    return s_client_started ? s_broker_uri : NULL;
}
