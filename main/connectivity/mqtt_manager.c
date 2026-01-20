#include "connectivity/mqtt_manager.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_system.h"
#include "mqtt_client.h"
#include "sdkconfig.h"

static const char *TAG = "mqtt";
static esp_mqtt_client_handle_t s_client;
static bool s_client_started;
static bool s_connected;
static char s_broker_uri[160];
static char s_client_id[13];
static mqtt_manager_status_cb_t s_status_cb;
static void *s_status_ctx;

#define MQTT_STATUS_BUFFER_LEN (96)
#define MQTT_TOPIC_MAX_LEN (160)
#define MQTT_DEVICE_SLUG_MAX_LEN (32)
#define MQTT_KEEPALIVE_SECONDS (10)

static char s_device_availability_topic[MQTT_TOPIC_MAX_LEN];
static char s_device_slug[MQTT_DEVICE_SLUG_MAX_LEN];
static char s_theo_base_topic[MQTT_TOPIC_MAX_LEN];

static esp_err_t build_device_availability_topic(void);
static void mqtt_manager_publish_device_availability(bool online);

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

static void mqtt_manager_status_update(const char *status)
{
    if (s_status_cb && status) {
        s_status_cb(status, s_status_ctx);
    }
}

static void mqtt_manager_status_error(const esp_mqtt_error_codes_t *err)
{
    if (!s_status_cb) {
        return;
    }

    char buffer[MQTT_STATUS_BUFFER_LEN];
    if (err && err->esp_transport_sock_errno != 0) {
        snprintf(buffer, sizeof(buffer), "Broker error (sock_errno=%d)", err->esp_transport_sock_errno);
    } else if (err && err->connect_return_code != 0) {
        snprintf(buffer, sizeof(buffer), "Broker error (rc=%d)", err->connect_return_code);
    } else {
        snprintf(buffer, sizeof(buffer), "Broker error");
    }
    s_status_cb(buffer, s_status_ctx);
}

static void trim_topic_slashes(const char *input, char *output, size_t output_len)
{
    if (input == NULL || output == NULL || output_len == 0) {
        if (output != NULL && output_len > 0) {
            output[0] = '\0';
        }
        return;
    }

    const char *start = input;
    while (*start != '\0' && isspace((unsigned char)*start)) {
        ++start;
    }

    const char *end = input + strlen(input);
    while (end > start && isspace((unsigned char)end[-1])) {
        --end;
    }

    while (start < end && *start == '/') {
        ++start;
    }

    while (end > start && end[-1] == '/') {
        --end;
    }

    size_t len = (size_t)(end - start);
    if (len == 0 || len >= output_len) {
        output[0] = '\0';
        return;
    }

    memcpy(output, start, len);
    output[len] = '\0';
}

static void normalize_slug(const char *input, char *output, size_t output_len)
{
    if (input == NULL || output == NULL || output_len == 0) {
        if (output != NULL && output_len > 0) {
            output[0] = '\0';
        }
        return;
    }

    size_t out_idx = 0;
    bool prev_was_dash = true;

    for (size_t i = 0; input[i] != '\0' && out_idx < output_len - 1; ++i) {
        unsigned char raw = (unsigned char)input[i];
        char c = (char)tolower(raw);
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
            output[out_idx++] = c;
            prev_was_dash = false;
        } else if (!prev_was_dash) {
            output[out_idx++] = '-';
            prev_was_dash = true;
        }
    }

    while (out_idx > 0 && output[out_idx - 1] == '-') {
        --out_idx;
    }

    output[out_idx] = '\0';
}

static esp_err_t build_device_availability_topic(void)
{
    char base_topic[MQTT_TOPIC_MAX_LEN];
    trim_topic_slashes(CONFIG_THEO_THEOSTAT_BASE_TOPIC, base_topic, sizeof(base_topic));
    if (base_topic[0] == '\0') {
        snprintf(base_topic, sizeof(base_topic), "theostat");
    }

    normalize_slug(CONFIG_THEO_DEVICE_SLUG, s_device_slug, sizeof(s_device_slug));
    if (s_device_slug[0] == '\0') {
        snprintf(s_device_slug, sizeof(s_device_slug), "hallway");
    }

    int written = snprintf(s_theo_base_topic, sizeof(s_theo_base_topic), "%s", base_topic);
    ESP_RETURN_ON_FALSE(written > 0 && written < (int)sizeof(s_theo_base_topic), ESP_ERR_INVALID_SIZE, TAG,
                        "Theo base topic overflow");

    written = snprintf(s_device_availability_topic,
                       sizeof(s_device_availability_topic),
                       "%s/%s/availability",
                       s_theo_base_topic,
                       s_device_slug);
    ESP_RETURN_ON_FALSE(written > 0 && written < (int)sizeof(s_device_availability_topic), ESP_ERR_INVALID_SIZE, TAG,
                        "Device availability topic overflow");

    ESP_LOGI(TAG, "Device availability topic: %s", s_device_availability_topic);
    return ESP_OK;
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;
    switch (event_id) {
    case MQTT_EVENT_CONNECTED:
        s_connected = true;
        ESP_LOGI(TAG, "[%s] CONNECTED (session=%d)", s_broker_uri, event->session_present);
        mqtt_manager_status_update("Broker connected");
        mqtt_manager_publish_device_availability(true);
        break;
    case MQTT_EVENT_DISCONNECTED:
        s_connected = false;
        ESP_LOGW(TAG, "[%s] DISCONNECTED", s_broker_uri);
        mqtt_manager_status_update("Broker disconnected");
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGD(TAG, "[%s] UNSUBSCRIBED msg_id=%d", s_broker_uri, event ? event->msg_id : -1);
        break;
    case MQTT_EVENT_ERROR:
        s_connected = false;
        ESP_LOGE(TAG, "[%s] ERROR event", s_broker_uri);
        log_error_details(event ? event->error_handle : NULL);
        mqtt_manager_status_error(event ? event->error_handle : NULL);
        break;
    case MQTT_EVENT_BEFORE_CONNECT:
        ESP_LOGD(TAG, "[%s] BEFORE_CONNECT", s_broker_uri);
        mqtt_manager_status_update("Connecting to broker…");
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
#if defined(CONFIG_MQTT_TRANSPORT_WEBSOCKET_SECURE)
    const char *scheme = "wss";
#else
    const char *scheme = "ws";
#endif
    int written = snprintf(s_broker_uri,
                           sizeof(s_broker_uri),
                           "%s://%s:%d%s%s",
                           scheme,
                           host,
                           CONFIG_THEO_MQTT_PORT,
                           path_present ? path_sep : "",
                           path_present ? path_cfg : "");
    ESP_RETURN_ON_FALSE(written > 0 && written < (int)sizeof(s_broker_uri), ESP_ERR_INVALID_SIZE, TAG,
                        "Broker URI overflow");
    ESP_LOGI(TAG, "MQTT URI %s (heap=%u, min_heap=%u)", s_broker_uri, esp_get_free_heap_size(), esp_get_minimum_free_heap_size());
    return ESP_OK;
}

static void mqtt_manager_publish_device_availability(bool online)
{
    if (!s_connected || s_client == NULL || s_device_availability_topic[0] == '\0') {
        return;
    }

    const char *payload = online ? "online" : "offline";
    int msg_id = esp_mqtt_client_publish(s_client, s_device_availability_topic, payload, 0, 0, 1);
    if (msg_id < 0) {
        ESP_LOGW(TAG, "Failed to publish device availability");
    } else {
        ESP_LOGI(TAG, "Published device availability: %s", payload);
    }
}

esp_err_t mqtt_manager_start(mqtt_manager_status_cb_t status_cb, void *ctx)
{
    if (s_client_started) {
        return ESP_OK;
    }

    s_status_cb = status_cb;
    s_status_ctx = ctx;

    ESP_LOGI(TAG,
             "log levels default=%d max=%d dynamic_control=%s",
             CONFIG_LOG_DEFAULT_LEVEL,
             CONFIG_LOG_MAXIMUM_LEVEL,
             CONFIG_LOG_DYNAMIC_LEVEL_CONTROL ? "y" : "n");

    ESP_RETURN_ON_ERROR(build_broker_uri(), TAG, "build URI failed");
    ESP_RETURN_ON_ERROR(build_device_availability_topic(), TAG, "build availability topic failed");

    uint32_t rand_bytes = 0;
    esp_fill_random(&rand_bytes, sizeof(rand_bytes));
    unsigned int b0 = (rand_bytes >> 0) & 0xFFu;
    unsigned int b1 = (rand_bytes >> 8) & 0xFFu;
    unsigned int b2 = (rand_bytes >> 16) & 0xFFu;
    unsigned int b3 = (rand_bytes >> 24) & 0xFFu;
    int written = snprintf(s_client_id,
                           sizeof(s_client_id),
                           "esp-%02X%02X%02X%02X",
                           b0,
                           b1,
                           b2,
                           b3);
    ESP_RETURN_ON_FALSE(written > 0 && written < (int)sizeof(s_client_id), ESP_ERR_INVALID_SIZE, TAG,
                        "client_id overflow");
    ESP_LOGI(TAG, "MQTT client_id=%s", s_client_id);

    esp_mqtt_client_config_t cfg = {
        .broker.address.uri = s_broker_uri,
        .credentials.client_id = s_client_id,
        .network.disable_auto_reconnect = false,
        .session.disable_clean_session = false,
        .session.keepalive = MQTT_KEEPALIVE_SECONDS,
        .session.last_will.topic = s_device_availability_topic,
        .session.last_will.msg = "offline",
        .session.last_will.msg_len = 0,
        .session.last_will.qos = 0,
        .session.last_will.retain = 1,
    };

    s_client = esp_mqtt_client_init(&cfg);
    if (s_client == NULL) {
        ESP_LOGE(TAG, "[%s] esp_mqtt_client_init returned NULL (heap=%u, min_heap=%u)",
                 s_broker_uri, esp_get_free_heap_size(), esp_get_minimum_free_heap_size());
        return ESP_ERR_NO_MEM;
    }

    esp_log_level_set("MQTT_CLIENT", ESP_LOG_DEBUG);
    ESP_LOGI(TAG, "forced log level for tag MQTT_CLIENT to DEBUG");

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
    ESP_LOGI(TAG, "[%s] MQTT client starting (id=%s)", s_broker_uri, s_client_id);
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

esp_mqtt_client_handle_t mqtt_manager_get_client(void)
{
    return s_client;
}
