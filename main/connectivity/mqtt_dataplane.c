#include "connectivity/mqtt_dataplane.h"

#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_lv_adapter.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "mqtt_client.h"
#include "sdkconfig.h"

#include "connectivity/mqtt_manager.h"
#include "thermostat/backlight_manager.h"
#include "thermostat/ui_actions.h"
#include "thermostat/ui_setpoint_view.h"
#include "thermostat/ui_state.h"
#include "thermostat/ui_top_bar.h"
#include "thermostat/remote_setpoint_controller.h"
#include "thermostat/thermostat_led_status.h"
#include "theo_device_identity.h"

LV_IMG_DECLARE(sunny);
LV_IMG_DECLARE(clear_night);
LV_IMG_DECLARE(partlycloudy);
LV_IMG_DECLARE(cloudy);
LV_IMG_DECLARE(fog);
LV_IMG_DECLARE(rainy);
LV_IMG_DECLARE(pouring);
LV_IMG_DECLARE(snowy);
LV_IMG_DECLARE(snowy_rainy);
LV_IMG_DECLARE(lightning);
LV_IMG_DECLARE(lightning_rainy);
LV_IMG_DECLARE(windy);
LV_IMG_DECLARE(windy_variant);
LV_IMG_DECLARE(hail);
LV_IMG_DECLARE(room_living);
LV_IMG_DECLARE(room_bedroom);
LV_IMG_DECLARE(room_office);
LV_IMG_DECLARE(room_hallway);
LV_IMG_DECLARE(room_default);

#define MQTT_DP_QUEUE_DEPTH            (20)
#define MQTT_DP_TASK_STACK             (8192)
#define MQTT_DP_TASK_PRIO              (5)
#define MQTT_DP_MAX_TOPIC_LEN          (160)
#define MQTT_DP_MAX_REASSEMBLY         (4)
#define MQTT_DP_COMMAND_TIMEOUT_MS     (3000)
#define MQTT_DP_MIN_SETPOINT_C         (10.0f)
#define MQTT_DP_MAX_SETPOINT_C         (35.0f)
#define MQTT_DP_VALUE_EPSILON          (0.05f)

static const char *TAG = "mqtt_dp";

typedef enum {
    DP_MSG_CONNECTED = 0,
    DP_MSG_DISCONNECTED,
    DP_MSG_FRAGMENT,
} dp_msg_type_t;

typedef enum {
    TOPIC_WEATHER_TEMP = 0,
    TOPIC_WEATHER_ICON,
    TOPIC_ROOM_TEMP,
    TOPIC_SETPOINT_LOW,
    TOPIC_SETPOINT_HIGH,
    TOPIC_ROOM_NAME,
    TOPIC_FAN_STATE,
    TOPIC_HEAT_STATE,
    TOPIC_COOL_STATE,
    TOPIC_COMMAND_PUBLISH,
} topic_id_t;

typedef struct {
    const char *suffix;
    topic_id_t id;
    bool subscribe;
    int qos;
    char topic[MQTT_DP_MAX_TOPIC_LEN];
    size_t topic_len;
} topic_desc_t;

typedef struct {
    int msg_id;
    size_t total_len;
    size_t filled;
    char *topic;
    size_t topic_len;
    char *buffer;
    int64_t timestamp_us;
} reassembly_state_t;

typedef struct {
    dp_msg_type_t type;
    union {
        struct {
            int msg_id;
            size_t total_len;
            size_t fragment_len;
            size_t offset;
            int64_t timestamp_us;
            char *topic;
            size_t topic_len;
            char *data;
        } fragment;
    } payload;
} dp_queue_msg_t;


static topic_desc_t s_topics[] = {
    {"sensor/pirateweather_temperature/state", TOPIC_WEATHER_TEMP, true, 0, "", 0},
    {"sensor/pirateweather_summary/state", TOPIC_WEATHER_ICON, true, 0, "", 0},
    {"sensor/thermostat_target_room_temperature/state", TOPIC_ROOM_TEMP, true, 0, "", 0},
    {"climate/theoretical_thermostat_ctrl_climate_control/target_temp_low", TOPIC_SETPOINT_LOW, true, 0, "", 0},
    {"climate/theoretical_thermostat_ctrl_climate_control/target_temp_high", TOPIC_SETPOINT_HIGH, true, 0, "", 0},
    {"sensor/thermostat_target_room_name/state", TOPIC_ROOM_NAME, true, 0, "", 0},
    {"binary_sensor/theoretical_thermostat_ctrl_computed_fan/state", TOPIC_FAN_STATE, true, 0, "", 0},
    {"binary_sensor/theoretical_thermostat_ctrl_computed_heat/state", TOPIC_HEAT_STATE, true, 0, "", 0},
    {"binary_sensor/theoretical_thermostat_ctrl_computed_a_c/state", TOPIC_COOL_STATE, true, 0, "", 0},
    {"temperature_command", TOPIC_COMMAND_PUBLISH, false, 1, "", 0},
};

static char s_theo_command_topic[MQTT_DP_MAX_TOPIC_LEN];
static bool s_command_topic_ready;

static QueueHandle_t s_msg_queue;
static TaskHandle_t s_task_handle;
static bool s_started;
static bool s_topics_initialized;
static reassembly_state_t s_reassembly[MQTT_DP_MAX_REASSEMBLY];

static void mqtt_dataplane_task(void *arg);
static void mqtt_dataplane_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data);
static void handle_connected_event(void);
static void handle_fragment_message(dp_queue_msg_t *msg);
static void init_topic_strings(void);
static const topic_desc_t *match_topic(const char *topic, size_t topic_len);
static void process_payload(const topic_desc_t *desc, char *payload, size_t payload_len, int64_t timestamp_us);
static void free_queue_message(dp_queue_msg_t *msg);
static void reset_reassembly_state(reassembly_state_t *state);
static reassembly_state_t *acquire_reassembly(int msg_id, size_t total_len);
static bool clamp_setpoint(float *value);
static const lv_img_dsc_t *icon_for_weather_icon_name(const char *summary);
static const lv_img_dsc_t *icon_for_room_name(const char *name, bool *is_error);
static bool parse_on_off(const char *value, bool *is_on);

esp_err_t mqtt_dataplane_start(void)
{
    if (s_started) {
        return ESP_OK;
    }

    ESP_LOGI(TAG,
             "mqtt_dataplane_start begin (queue=%p task=%p started=%d topics_init=%d)",
             (void *)s_msg_queue,
             (void *)s_task_handle,
             s_started,
             s_topics_initialized);

    esp_mqtt_client_handle_t client = mqtt_manager_get_client();
    ESP_RETURN_ON_FALSE(client != NULL, ESP_ERR_INVALID_STATE, TAG, "MQTT client not ready");

    s_msg_queue = xQueueCreate(MQTT_DP_QUEUE_DEPTH, sizeof(dp_queue_msg_t));
    ESP_RETURN_ON_FALSE(s_msg_queue != NULL, ESP_ERR_NO_MEM, TAG, "queue alloc failed");
    ESP_LOGI(TAG,
             "dataplane queue created depth=%d item_size=%zu handle=%p",
             MQTT_DP_QUEUE_DEPTH,
             sizeof(dp_queue_msg_t),
             (void *)s_msg_queue);

    init_topic_strings();
    ESP_LOGI(TAG, "topic strings initialized (HA base=%s Theo base=%s)", CONFIG_THEO_HA_BASE_TOPIC, theo_identity_base_topic());

    esp_err_t err = esp_mqtt_client_register_event(client, MQTT_EVENT_ANY, mqtt_dataplane_event_handler, NULL);
    if (err != ESP_OK) {
        vQueueDelete(s_msg_queue);
        s_msg_queue = NULL;
        return err;
    }
    ESP_LOGI(TAG, "mqtt_dataplane_event_handler registered (client=%p)", (void *)client);

    BaseType_t task_ok = xTaskCreate(mqtt_dataplane_task, "mqtt_dp", MQTT_DP_TASK_STACK, NULL, MQTT_DP_TASK_PRIO, &s_task_handle);
    if (task_ok != pdPASS) {
        esp_mqtt_client_unregister_event(client, MQTT_EVENT_ANY, mqtt_dataplane_event_handler);
        vQueueDelete(s_msg_queue);
        s_msg_queue = NULL;
        ESP_LOGE(TAG, "failed to create dataplane task");
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG,
             "dataplane task created name=%s stack=%d prio=%d handle=%p",
             "mqtt_dp",
             MQTT_DP_TASK_STACK,
             MQTT_DP_TASK_PRIO,
             (void *)s_task_handle);

    s_started = true;
    ESP_LOGI(TAG, "dataplane task ready (base=%s)", CONFIG_THEO_HA_BASE_TOPIC);

    if (mqtt_manager_is_ready()) {
        dp_queue_msg_t ready_msg = {
            .type = DP_MSG_CONNECTED,
        };
        if (xQueueSend(s_msg_queue, &ready_msg, 0) == pdTRUE) {
            ESP_LOGI(TAG, "mqtt manager already connected; injected DP_MSG_CONNECTED for subscriptions");
        } else {
            ESP_LOGW(TAG, "mqtt manager connected but queue full; subscriptions will wait for next event");
        }
    } else {
        ESP_LOGI(TAG, "mqtt manager not yet connected; waiting for MQTT_EVENT_CONNECTED to subscribe");
    }
    return ESP_OK;
}

esp_err_t mqtt_dataplane_publish_temperature_command(float cooling_setpoint_c, float heating_setpoint_c)
{
    esp_mqtt_client_handle_t client = mqtt_manager_get_client();
    ESP_RETURN_ON_FALSE(client != NULL && mqtt_manager_is_ready(), ESP_ERR_INVALID_STATE, TAG, "MQTT client unavailable");

    float high = cooling_setpoint_c;
    float low = heating_setpoint_c;
    bool high_clamped = clamp_setpoint(&high);
    bool low_clamped = clamp_setpoint(&low);
    if (high_clamped || low_clamped) {
        ESP_LOGW(TAG, "command clamp applied high=%.2f low=%.2f", high, low);
    }
    if (high < (low + THERMOSTAT_TEMP_STEP_C)) {
        ESP_LOGE(TAG, "command invalid: high %.2f < low %.2f + step", high, low);
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_FALSE(s_command_topic_ready, ESP_ERR_INVALID_STATE, TAG, "command topic not ready");

    char payload[128];
    int written = snprintf(payload,
                           sizeof(payload),
                           "{\"target_temp_high\":%.2f,\"target_temp_low\":%.2f}",
                           high,
                           low);
    ESP_RETURN_ON_FALSE(written > 0 && written < (int)sizeof(payload), ESP_ERR_INVALID_SIZE, TAG, "payload overflow");

    int msg_id = esp_mqtt_client_publish(client, s_theo_command_topic, payload, 0, 1, 0);
    if (msg_id < 0) {
        ESP_LOGE(TAG, "temperature_command publish failed (err=%d)", msg_id);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "temperature_command msg_id=%d payload=%s", msg_id, payload);
    return ESP_OK;
}

static void mqtt_dataplane_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    if (!s_started || s_msg_queue == NULL) {
        return;
    }
    esp_mqtt_event_handle_t event = event_data;
    dp_queue_msg_t msg = {0};

    switch (event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED msg_id=%d", event ? event->msg_id : -1);
        msg.type = DP_MSG_CONNECTED;
        if (xQueueSend(s_msg_queue, &msg, 0) != pdTRUE) {
            ESP_LOGW(TAG, "queue send failed (MQTT_EVENT_CONNECTED)");
        }
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "MQTT_EVENT_DISCONNECTED msg_id=%d", event ? event->msg_id : -1);
        msg.type = DP_MSG_DISCONNECTED;
        if (xQueueSend(s_msg_queue, &msg, 0) != pdTRUE) {
            ESP_LOGW(TAG, "queue send failed (MQTT_EVENT_DISCONNECTED)");
        }
        break;
    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "MQTT_EVENT_DATA msg_id=%d total=%d offset=%d len=%d topic_len=%d",
                 event->msg_id,
                 event->total_data_len,
                 event->current_data_offset,
                 event->data_len,
                 event->topic_len);
        ESP_LOGI(TAG,
                 "RX raw topic_ptr=%p topic_len=%d data_ptr=%p data_len=%d",
                 event->topic,
                 event->topic_len,
                 event->data,
                 event->data_len);
        if (event->topic && event->topic_len > 0) {
            ESP_LOG_BUFFER_CHAR_LEVEL(TAG, event->topic, event->topic_len, ESP_LOG_INFO);
        }
        if (event->data && event->data_len > 0) {
            ESP_LOG_BUFFER_HEX_LEVEL(TAG, event->data, event->data_len, ESP_LOG_INFO);
        }
        msg.type = DP_MSG_FRAGMENT;
        msg.payload.fragment.msg_id = event->msg_id;
        msg.payload.fragment.total_len = (event->total_data_len > 0) ? event->total_data_len : event->data_len;
        msg.payload.fragment.fragment_len = event->data_len;
        msg.payload.fragment.offset = event->current_data_offset;
        msg.payload.fragment.timestamp_us = esp_timer_get_time();
        msg.payload.fragment.topic_len = event->topic_len;
        msg.payload.fragment.topic = NULL;
        msg.payload.fragment.data = NULL;
        if (event->topic && event->topic_len > 0 && event->current_data_offset == 0) {
            msg.payload.fragment.topic = malloc(event->topic_len + 1);
            if (msg.payload.fragment.topic) {
                memcpy(msg.payload.fragment.topic, event->topic, event->topic_len);
                msg.payload.fragment.topic[event->topic_len] = '\0';
            }
        }
        if (event->data_len > 0) {
            msg.payload.fragment.data = malloc(event->data_len);
            if (msg.payload.fragment.data) {
                memcpy(msg.payload.fragment.data, event->data, event->data_len);
            }
        }
        if (msg.payload.fragment.data == NULL) {
            free(msg.payload.fragment.topic);
            msg.payload.fragment.topic = NULL;
            ESP_LOGE(TAG, "alloc failed for MQTT fragment data len=%d", event->data_len);
            break;
        }
        if (xQueueSend(s_msg_queue, &msg, 0) != pdTRUE) {
            ESP_LOGW(TAG, "dropping MQTT fragment (queue full) msg_id=%d", event->msg_id);
            free(msg.payload.fragment.topic);
            free(msg.payload.fragment.data);
        }
        break;
    default:
        ESP_LOGW(TAG, "Unhandled MQTT event_id=%ld", (long)event_id);
        break;
    }
}

static void mqtt_dataplane_task(void *arg)
{
    dp_queue_msg_t msg = {0};
    while (true) {
        if (xQueueReceive(s_msg_queue, &msg, portMAX_DELAY) != pdTRUE) {
            ESP_LOGW(TAG, "dataplane queue receive failed");
            continue;
        }
        switch (msg.type) {
        case DP_MSG_CONNECTED:
            handle_connected_event();
            break;
        case DP_MSG_FRAGMENT:
            handle_fragment_message(&msg);
            break;
        default:
            ESP_LOGW(TAG, "Unhandled queue msg type=%d", msg.type);
            break;
        }
        free_queue_message(&msg);
        memset(&msg, 0, sizeof(msg));
    }
}

static void handle_connected_event(void)
{
    esp_mqtt_client_handle_t client = mqtt_manager_get_client();
    if (client == NULL) {
        ESP_LOGW(TAG, "handle_connected_event called but client is NULL");
        return;
    }
    size_t topic_count = sizeof(s_topics) / sizeof(s_topics[0]);
    ESP_LOGI(TAG, "handle_connected_event client=%p subscribing_to=%zu topics", (void *)client, topic_count);
    for (size_t i = 0; i < sizeof(s_topics) / sizeof(s_topics[0]); ++i) {
        if (!s_topics[i].subscribe) {
            continue;
        }
        int msg_id = esp_mqtt_client_subscribe(client, s_topics[i].topic, s_topics[i].qos);
        if (msg_id < 0) {
            ESP_LOGW(TAG, "subscribe failed topic=%s", s_topics[i].topic);
        } else {
            ESP_LOGI(TAG, "subscribed topic=%s msg_id=%d", s_topics[i].topic, msg_id);
        }
    }
}

static void handle_fragment_message(dp_queue_msg_t *msg)
{
    dp_queue_msg_t *m = msg;
    if (m->payload.fragment.total_len <= m->payload.fragment.fragment_len) {
        char *topic = m->payload.fragment.topic;
        char *data = m->payload.fragment.data;
        if (topic == NULL || data == NULL) {
            free(topic);
            free(data);
            m->payload.fragment.topic = NULL;
            m->payload.fragment.data = NULL;
            return;
        }
        ESP_LOGI(TAG, "fragment topic=%.*s offset=%zu len=%zu payload=%.*s",
                 (int)m->payload.fragment.topic_len,
                 topic,
                 m->payload.fragment.offset,
                 m->payload.fragment.fragment_len,
                 (int)m->payload.fragment.fragment_len,
                 data);
        process_payload(match_topic(topic, strlen(topic)), data, m->payload.fragment.fragment_len, m->payload.fragment.timestamp_us);
        free(topic);
        free(data);
        m->payload.fragment.topic = NULL;
        m->payload.fragment.data = NULL;
        return;
    }

    reassembly_state_t *state = acquire_reassembly(m->payload.fragment.msg_id, m->payload.fragment.total_len);
    if (state == NULL) {
        ESP_LOGW(TAG, "reassembly slots exhausted (msg_id=%d)", m->payload.fragment.msg_id);
        return;
    }
    if (m->payload.fragment.topic != NULL && state->topic == NULL) {
        state->topic = m->payload.fragment.topic;
        state->topic_len = m->payload.fragment.topic_len;
        m->payload.fragment.topic = NULL;
    }
    if (state->buffer == NULL) {
        state->buffer = calloc(1, state->total_len + 1);
        if (state->buffer == NULL) {
            ESP_LOGE(TAG, "alloc failed for MQTT payload (%zu bytes)", state->total_len);
            reset_reassembly_state(state);
            return;
        }
    }
    memcpy(state->buffer + m->payload.fragment.offset, m->payload.fragment.data, m->payload.fragment.fragment_len);
    state->filled += m->payload.fragment.fragment_len;
    if (state->timestamp_us == 0) {
        state->timestamp_us = m->payload.fragment.timestamp_us;
    }

    if (state->filled >= state->total_len && state->topic != NULL) {
        state->buffer[state->total_len] = '\0';
        ESP_LOGI(TAG, "reassembled topic=%s len=%zu payload=%s", state->topic, state->total_len, state->buffer);
        process_payload(match_topic(state->topic, state->topic_len), state->buffer, state->total_len, state->timestamp_us);
        reset_reassembly_state(state);
    }
}

static void free_queue_message(dp_queue_msg_t *msg)
{
    if (msg->type == DP_MSG_FRAGMENT) {
        free(msg->payload.fragment.topic);
        free(msg->payload.fragment.data);
        msg->payload.fragment.topic = NULL;
        msg->payload.fragment.data = NULL;
    }
}

static void reset_reassembly_state(reassembly_state_t *state)
{
    if (state == NULL) {
        return;
    }
    free(state->topic);
    free(state->buffer);
    memset(state, 0, sizeof(*state));
}

static reassembly_state_t *acquire_reassembly(int msg_id, size_t total_len)
{
    for (size_t i = 0; i < MQTT_DP_MAX_REASSEMBLY; ++i) {
        if (s_reassembly[i].msg_id == msg_id) {
            return &s_reassembly[i];
        }
    }
    for (size_t i = 0; i < MQTT_DP_MAX_REASSEMBLY; ++i) {
        if (s_reassembly[i].msg_id == 0) {
            s_reassembly[i].msg_id = msg_id;
            s_reassembly[i].total_len = total_len;
            s_reassembly[i].filled = 0;
            s_reassembly[i].timestamp_us = 0;
            return &s_reassembly[i];
        }
    }
    return NULL;
}

static void init_topic_strings(void)
{
    if (s_topics_initialized) {
        return;
    }
    char base[MQTT_DP_MAX_TOPIC_LEN] = {0};
    const char *cfg_base = CONFIG_THEO_HA_BASE_TOPIC;
    if (cfg_base == NULL || cfg_base[0] == '\0') {
        strncpy(base, "homeassistant", sizeof(base) - 1);
    } else {
        strncpy(base, cfg_base, sizeof(base) - 1);
    }
    size_t len = strlen(base);
    while (len > 0 && base[len - 1] == '/') {
        base[len - 1] = '\0';
        --len;
    }

    const theo_identity_t *identity = theo_identity_get();
    if (identity == NULL) {
        ESP_LOGE(TAG, "Theo identity not initialized; cannot build command topic");
        return;
    }

    for (size_t i = 0; i < sizeof(s_topics) / sizeof(s_topics[0]); ++i) {
        const char *suffix = s_topics[i].suffix != NULL ? s_topics[i].suffix : "";
        const char *topic_base = (s_topics[i].id == TOPIC_COMMAND_PUBLISH) ? identity->base_topic : base;
        size_t base_len = strlen(topic_base);
        size_t suffix_len = strlen(suffix);
        bool add_slash = (suffix_len > 0);
        size_t needed = base_len + (add_slash ? 1 : 0) + suffix_len + 1;
        if (needed > sizeof(s_topics[i].topic)) {
            ESP_LOGE(TAG, "topic overflow for suffix=%s", suffix);
            s_topics[i].topic[0] = '\0';
            s_topics[i].topic_len = 0;
            continue;
        }
        size_t offset = 0;
        memcpy(s_topics[i].topic + offset, topic_base, base_len);
        offset += base_len;
        if (add_slash) {
            s_topics[i].topic[offset++] = '/';
            memcpy(s_topics[i].topic + offset, suffix, suffix_len);
            offset += suffix_len;
        }
        s_topics[i].topic[offset] = '\0';
        s_topics[i].topic_len = strlen(s_topics[i].topic);
    }
    strncpy(s_theo_command_topic, s_topics[TOPIC_COMMAND_PUBLISH].topic, sizeof(s_theo_command_topic) - 1);
    s_command_topic_ready = s_theo_command_topic[0] != '\0';
    s_topics_initialized = true;
}

static const topic_desc_t *match_topic(const char *topic, size_t topic_len)
{
    if (topic == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < sizeof(s_topics) / sizeof(s_topics[0]); ++i) {
        if (topic_len == s_topics[i].topic_len && strncmp(topic, s_topics[i].topic, topic_len) == 0) {
            return &s_topics[i];
        }
    }
    return NULL;
}

static bool clamp_setpoint(float *value)
{
    bool clamped = false;
    if (*value < MQTT_DP_MIN_SETPOINT_C) {
        *value = MQTT_DP_MIN_SETPOINT_C;
        clamped = true;
    }
    if (*value > MQTT_DP_MAX_SETPOINT_C) {
        *value = MQTT_DP_MAX_SETPOINT_C;
        clamped = true;
    }
    return clamped;
}

static bool parse_on_off(const char *value, bool *is_on)
{
    if (value == NULL || is_on == NULL) {
        return false;
    }
    if (strcasecmp(value, "on") == 0) {
        *is_on = true;
        return true;
    }
    if (strcasecmp(value, "off") == 0) {
        *is_on = false;
        return true;
    }
    return false;
}

static const lv_img_dsc_t *icon_for_weather_icon_name(const char *summary)
{
    if (summary == NULL) {
        return NULL;
    }
    struct mapping { const char *name; const lv_img_dsc_t *img; } map[] = {
      {"sunny", &sunny},
      {"clear-night", &clear_night},
      {"mostly-clear", &clear_night},
      {"partly-cloudy", &partlycloudy},
      {"cloudy", &cloudy},
      {"mostly-cloudy", &cloudy},
      {"fog", &fog},
      {"rainy", &rainy},
      {"pouring", &pouring},
      {"snowy", &snowy},
      {"snowy-rainy", &snowy_rainy},
      {"lightning", &lightning},
      {"lightning-rainy", &lightning_rainy},
      {"windy", &windy},
      {"windy-variant", &windy_variant},
      {"hail", &hail},
    };
    for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); ++i) {
        if (strcmp(summary, map[i].name) == 0) {
            return map[i].img;
        }
    }
    return NULL;
}

static const lv_img_dsc_t *icon_for_room_name(const char *name, bool *is_error)
{
    if (is_error) {
        *is_error = false;
    }
    if (name == NULL) {
        if (is_error) {
            *is_error = true;
        }
        return &room_default;
    }
    struct mapping { const char *name; const lv_img_dsc_t *img; } map[] = {
        {"Living Room", &room_living},
        {"Bedroom", &room_bedroom},
        {"Office", &room_office},
        {"Hallway", &room_hallway},
    };
    for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); ++i) {
        if (strcmp(name, map[i].name) == 0) {
            return map[i].img;
        }
    }
    if (is_error) {
        *is_error = true;
    }
    return &room_default;
}

static bool parse_number_payload(const char *payload, float *out_value)
{
    if (payload == NULL || out_value == NULL) {
        return false;
    }
    char *end = NULL;
    float value = strtof(payload, &end);
    if (end == payload) {
        return false;
    }
    while (end && *end != '\0' && isspace((unsigned char)*end)) {
        end++;
    }
    if (end && *end != '\0') {
        return false;
    }
    if (!isfinite(value)) {
        return false;
    }
    *out_value = value;
    return true;
}

static void process_payload(const topic_desc_t *desc, char *payload, size_t payload_len, int64_t timestamp_us)
{
    (void)timestamp_us;
    if (desc == NULL || payload == NULL) {
        return;
    }
    char buffer[192];
    size_t copy_len = (payload_len < (sizeof(buffer) - 1)) ? payload_len : (sizeof(buffer) - 1);
    memcpy(buffer, payload, copy_len);
    buffer[copy_len] = '\0';

    switch (desc->id) {
    case TOPIC_WEATHER_TEMP: {
        float value = 0.0f;
        bool ok = parse_number_payload(buffer, &value);
        if (esp_lv_adapter_lock(-1) == ESP_OK) {
            g_view_model.weather_ready = true;
            g_view_model.weather_temp_valid = ok;
            if (ok) {
                g_view_model.weather_temp_c = value;
            }
            thermostat_update_weather_group();
            esp_lv_adapter_unlock();
        } else {
            ESP_LOGW(TAG, "LVGL lock timeout updating weather temp");
        }
        if (!ok) {
            ESP_LOGW(TAG, "invalid weather temperature payload");
        }
        break;
    }
    case TOPIC_WEATHER_ICON: {
        const lv_img_dsc_t *icon = icon_for_weather_icon_name(buffer);
        if (esp_lv_adapter_lock(-1) == ESP_OK) {
            g_view_model.weather_ready = true;
            g_view_model.weather_icon = icon;
            thermostat_update_weather_group();
            esp_lv_adapter_unlock();
        } else {
            ESP_LOGW(TAG, "LVGL lock timeout updating weather summary");
        }
        if (icon == NULL && buffer[0] != '\0') {
            ESP_LOGW(TAG, "invalid weather summary payload");
        }
        break;
    }
    case TOPIC_ROOM_TEMP: {
        float value = 0.0f;
        bool ok = parse_number_payload(buffer, &value);
        if (esp_lv_adapter_lock(-1) == ESP_OK) {
            g_view_model.room_ready = true;
            g_view_model.room_temp_valid = ok;
            if (ok) {
                g_view_model.room_temp_c = value;
            }
            thermostat_update_room_group();
            esp_lv_adapter_unlock();
        } else {
            ESP_LOGW(TAG, "LVGL lock timeout updating room temp");
        }
        if (!ok) {
            ESP_LOGW(TAG, "invalid room temperature payload");
        }
        break;
    }
    case TOPIC_ROOM_NAME: {
        bool error = false;
        const lv_img_dsc_t *icon = icon_for_room_name(buffer[0] ? buffer : NULL, &error);
        if (esp_lv_adapter_lock(-1) == ESP_OK) {
            g_view_model.room_ready = true;
            g_view_model.room_icon = icon;
            g_view_model.room_icon_error = error;
            thermostat_update_room_group();
            esp_lv_adapter_unlock();
        } else {
            ESP_LOGW(TAG, "LVGL lock timeout updating room icon");
        }
        if (!buffer[0]) {
            ESP_LOGW(TAG, "invalid room name payload");
        }
        break;
    }
    case TOPIC_FAN_STATE: {
        bool on = false;
        bool ok = parse_on_off(buffer, &on);
        if (esp_lv_adapter_lock(-1) == ESP_OK) {
            if (ok) {
                g_view_model.fan_running = on;
                g_view_model.fan_payload_error = false;
            } else {
                g_view_model.fan_payload_error = true;
            }
            thermostat_update_action_bar_visuals();
            esp_lv_adapter_unlock();
        } else {
            ESP_LOGW(TAG, "LVGL lock timeout updating fan state");
        }
        if (!ok) {
            ESP_LOGW(TAG, "invalid fan payload");
        }
        break;
    }
    case TOPIC_HEAT_STATE:
    case TOPIC_COOL_STATE: {
        bool on = false;
        bool ok = parse_on_off(buffer, &on);
        bool reported = false;
        bool heating = false;
        bool cooling = false;
        if (esp_lv_adapter_lock(-1) == ESP_OK) {
            g_view_model.hvac_ready = true;
            if (ok) {
                if (desc->id == TOPIC_HEAT_STATE) {
                    g_view_model.hvac_heating_active = on;
                } else {
                    g_view_model.hvac_cooling_active = on;
                }
                g_view_model.hvac_status_error = false;
                heating = g_view_model.hvac_heating_active;
                cooling = g_view_model.hvac_cooling_active;
                reported = true;
            } else {
                g_view_model.hvac_status_error = true;
            }
            thermostat_update_hvac_status_group();
            esp_lv_adapter_unlock();
        } else {
            ESP_LOGW(TAG, "LVGL lock timeout updating HVAC state");
        }
        if (!ok) {
            ESP_LOGW(TAG, "invalid HVAC payload (%s)", desc->topic);
        } else if (reported) {
            thermostat_led_status_set_hvac(heating, cooling);
        }
        break;
    }
    case TOPIC_SETPOINT_LOW:
    case TOPIC_SETPOINT_HIGH: {
        float value = 0.0f;
        bool ok = parse_number_payload(buffer, &value);
        bool clamped = false;
        if (ok) {
            clamped = clamp_setpoint(&value);
        }
        thermostat_target_t target = (desc->id == TOPIC_SETPOINT_HIGH) ? THERMOSTAT_TARGET_COOL
                                                                      : THERMOSTAT_TARGET_HEAT;
        if (!ok) {
            ESP_LOGW(TAG, "invalid %s payload", desc->topic);
            if (esp_lv_adapter_lock(-1) == ESP_OK) {
                if (target == THERMOSTAT_TARGET_COOL) {
                    g_view_model.cooling_setpoint_valid = false;
                } else {
                    g_view_model.heating_setpoint_valid = false;
                }
                thermostat_update_setpoint_labels();
                esp_lv_adapter_unlock();
            }
            break;
        }
        if (clamped) {
            ESP_LOGW(TAG, "%s clamped to %.2f", desc->topic, value);
        }
        if (esp_lv_adapter_lock(-1) == ESP_OK) {
            thermostat_remote_setpoint_controller_submit(target, value);
            esp_lv_adapter_unlock();
        } else {
            ESP_LOGW(TAG, "LVGL lock timeout applying remote setpoint");
        }
        break;
    }
    default:
        ESP_LOGW(TAG, "Unhandled topic id=%d", desc->id);
        break;
    }

    return;
}
