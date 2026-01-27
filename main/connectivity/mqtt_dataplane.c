#include "connectivity/mqtt_dataplane.h"

#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_lv_adapter.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "mqtt_client.h"
#include "sdkconfig.h"

#include "connectivity/mqtt_manager.h"
#include "sensors/env_sensors.h"
#include "thermostat/backlight_manager.h"
#include "thermostat/ui_actions.h"
#include "thermostat/ui_setpoint_view.h"
#include "thermostat/ui_state.h"
#include "thermostat/ui_top_bar.h"
#include "thermostat/remote_setpoint_controller.h"
#include "thermostat/thermostat_led_status.h"
#include "thermostat/thermostat_personal_presence.h"

LV_IMG_DECLARE(breezy);
LV_IMG_DECLARE(clear_day);
LV_IMG_DECLARE(clear_night);
LV_IMG_DECLARE(cloudy);
LV_IMG_DECLARE(dangerous_wind);
LV_IMG_DECLARE(drizzle);
LV_IMG_DECLARE(flurries);
LV_IMG_DECLARE(fog);
LV_IMG_DECLARE(haze);
LV_IMG_DECLARE(heavy_rain);
LV_IMG_DECLARE(heavy_sleet);
LV_IMG_DECLARE(heavy_snow);
LV_IMG_DECLARE(light_rain);
LV_IMG_DECLARE(light_sleet);
LV_IMG_DECLARE(light_snow);
LV_IMG_DECLARE(mist);
LV_IMG_DECLARE(mostly_clear_day);
LV_IMG_DECLARE(mostly_clear_night);
LV_IMG_DECLARE(mostly_cloudy_day);
LV_IMG_DECLARE(mostly_cloudy_night);
LV_IMG_DECLARE(partly_cloudy_day);
LV_IMG_DECLARE(partly_cloudy_night);
LV_IMG_DECLARE(possible_precipitation_day);
LV_IMG_DECLARE(possible_precipitation_night);
LV_IMG_DECLARE(possible_rain_day);
LV_IMG_DECLARE(possible_rain_night);
LV_IMG_DECLARE(possible_sleet_day);
LV_IMG_DECLARE(possible_sleet_night);
LV_IMG_DECLARE(possible_snow_day);
LV_IMG_DECLARE(possible_snow_night);
LV_IMG_DECLARE(possible_thunderstorm_day);
LV_IMG_DECLARE(possible_thunderstorm_night);
LV_IMG_DECLARE(precipitation);
LV_IMG_DECLARE(rain);
LV_IMG_DECLARE(sleet);
LV_IMG_DECLARE(smoke);
LV_IMG_DECLARE(snow);
LV_IMG_DECLARE(thunderstorm);
LV_IMG_DECLARE(very_light_sleet);
LV_IMG_DECLARE(wind);

LV_IMG_DECLARE(snowflake);
LV_IMG_DECLARE(wind);

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
#define MQTT_DP_STATUS_BUFFER_LEN      (96)

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
    TOPIC_PERSONAL_FACE,
    TOPIC_PERSONAL_COUNT,
    TOPIC_COMMAND,
} topic_id_t;

typedef struct {
    const char *suffix;
    topic_id_t id;
    bool subscribe;
    int qos;
    bool seen;
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
    bool retain_flag;
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
            bool retained;
        } fragment;
    } payload;
} dp_queue_msg_t;


static topic_desc_t s_topics[] = {
    {"sensor/pirateweather_temperature/state", TOPIC_WEATHER_TEMP, true, 0, false, "", 0},
    {"sensor/pirateweather_icon/state", TOPIC_WEATHER_ICON, true, 0, false, "", 0},
    {"sensor/theoretical_thermostat_target_room_temperature/state", TOPIC_ROOM_TEMP, true, 0, false, "", 0},
    {"climate/theoretical_thermostat_climate_control/target_temp_low", TOPIC_SETPOINT_LOW, true, 0, false, "", 0},
    {"climate/theoretical_thermostat_climate_control/target_temp_high", TOPIC_SETPOINT_HIGH, true, 0, false, "", 0},
    {"sensor/theoretical_thermostat_target_room_name/state", TOPIC_ROOM_NAME, true, 0, false, "", 0},
    {"binary_sensor/theoretical_thermostat_computed_fan/state", TOPIC_FAN_STATE, true, 0, false, "", 0},
    {"binary_sensor/theoretical_thermostat_computed_heat/state", TOPIC_HEAT_STATE, true, 0, false, "", 0},
    {"binary_sensor/theoretical_thermostat_computed_a_c/state", TOPIC_COOL_STATE, true, 0, false, "", 0},
    {"sensor/hallway_camera_last_recognized_face/state", TOPIC_PERSONAL_FACE, true, 0, false, "", 0},
    {"sensor/hallway_camera_person_count/state", TOPIC_PERSONAL_COUNT, true, 0, false, "", 0},
};

static QueueHandle_t s_msg_queue;
static TaskHandle_t s_task_handle;
static bool s_started;
static bool s_topics_initialized;
static EXT_RAM_BSS_ATTR reassembly_state_t s_reassembly[MQTT_DP_MAX_REASSEMBLY];

// Command topic uses Theo base (not HA base), so stored separately
static EXT_RAM_BSS_ATTR char s_command_topic[MQTT_DP_MAX_TOPIC_LEN];
static size_t s_command_topic_len;

static void mqtt_dataplane_task(void *arg);
static void mqtt_dataplane_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data);
static void handle_connected_event(void);
static void handle_fragment_message(dp_queue_msg_t *msg);
static void init_topic_strings(void);
static topic_desc_t *match_topic(const char *topic, size_t topic_len);
static void process_payload(topic_desc_t *desc, char *payload, size_t payload_len, bool retained, int64_t timestamp_us);
static void free_queue_message(dp_queue_msg_t *msg);
static void reset_reassembly_state(reassembly_state_t *state);
static reassembly_state_t *acquire_reassembly(int msg_id, size_t total_len);
static bool clamp_setpoint(float *value);
static const lv_img_dsc_t *icon_for_weather_icon_name(const char *summary);
static const lv_img_dsc_t *icon_for_room_name(const char *name, bool *is_error);
static bool parse_on_off(const char *value, bool *is_on);
static void format_missing_state(char *buffer,
                                 size_t buffer_len,
                                 bool weather_ready,
                                 bool room_ready,
                                 bool hvac_ready);

esp_err_t mqtt_dataplane_start(mqtt_dataplane_status_cb_t status_cb, void *ctx)
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

    thermostat_personal_presence_init();

    init_topic_strings();
    ESP_LOGI(TAG, "topic strings initialized (count=%zu)", sizeof(s_topics) / sizeof(s_topics[0]));

    esp_err_t err = esp_mqtt_client_register_event(client, MQTT_EVENT_ANY, mqtt_dataplane_event_handler, NULL);
    if (err != ESP_OK) {
        vQueueDelete(s_msg_queue);
        s_msg_queue = NULL;
        return err;
    }
    ESP_LOGI(TAG, "mqtt_dataplane_event_handler registered (client=%p)", (void *)client);

    BaseType_t task_ok = xTaskCreatePinnedToCoreWithCaps(mqtt_dataplane_task,
                                                        "mqtt_dp",
                                                        MQTT_DP_TASK_STACK,
                                                        NULL,
                                                        MQTT_DP_TASK_PRIO,
                                                        &s_task_handle,
                                                        tskNO_AFFINITY,
                                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
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
        if (status_cb) {
            status_cb("Waiting for MQTT connection...", ctx);
        }
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

    // Build command topic using Theo base topic
    const char *theo_base = env_sensors_get_theo_base_topic();
    ESP_RETURN_ON_FALSE(theo_base != NULL && theo_base[0] != '\0', ESP_ERR_INVALID_STATE, TAG, "Theo base topic not initialized");

    char command_topic[MQTT_DP_MAX_TOPIC_LEN];
    int topic_len = snprintf(command_topic, sizeof(command_topic),
                             "%s/climate/temperature_command", theo_base);
    ESP_RETURN_ON_FALSE(topic_len > 0 && topic_len < (int)sizeof(command_topic), ESP_ERR_INVALID_SIZE, TAG, "topic overflow");

    char payload[128];
    int written = snprintf(payload,
                           sizeof(payload),
                           "{\"target_temp_high\":%.2f,\"target_temp_low\":%.2f}",
                           high,
                           low);
    ESP_RETURN_ON_FALSE(written > 0 && written < (int)sizeof(payload), ESP_ERR_INVALID_SIZE, TAG, "payload overflow");

    int msg_id = esp_mqtt_client_publish(client, command_topic, payload, 0, 1, 0);
    if (msg_id < 0) {
        ESP_LOGE(TAG, "temperature_command publish failed (err=%d)", msg_id);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "temperature_command msg_id=%d topic=%s payload=%s", msg_id, command_topic, payload);
    return ESP_OK;
}

esp_err_t mqtt_dataplane_await_initial_state(mqtt_dataplane_status_cb_t status_cb,
                                             void *ctx,
                                             uint32_t timeout_ms)
{
    if (!s_started) {
        return ESP_ERR_INVALID_STATE;
    }

    const TickType_t poll_interval = pdMS_TO_TICKS(100);
    const TickType_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);
    const TickType_t log_interval = pdMS_TO_TICKS(5000);
    TickType_t elapsed = 0;
    TickType_t last_log = 0;
    int64_t start_us = esp_timer_get_time();

    bool prev_weather = false;
    bool prev_room = false;
    bool prev_hvac = false;
    bool notified_waiting = false;

    ESP_LOGI(TAG, "awaiting initial state timeout=%ums", (unsigned)timeout_ms);

    while (elapsed < timeout_ticks) {
        bool weather_ready = false;
        bool room_ready = false;
        bool hvac_ready = false;

        if (esp_lv_adapter_lock(100) == ESP_OK) {
            weather_ready = g_view_model.weather_ready;
            room_ready = g_view_model.room_ready;
            hvac_ready = g_view_model.hvac_ready;
            esp_lv_adapter_unlock();
        }

        bool state_changed = (weather_ready != prev_weather) ||
                             (room_ready != prev_room) ||
                             (hvac_ready != prev_hvac);

        if (weather_ready && !prev_weather) {
            ESP_LOGI(TAG, "initial state: weather ready");
        }
        if (room_ready && !prev_room) {
            ESP_LOGI(TAG, "initial state: room ready");
        }
        if (hvac_ready && !prev_hvac) {
            ESP_LOGI(TAG, "initial state: HVAC ready");
        }

        if (weather_ready && room_ready && hvac_ready) {
            int64_t elapsed_ms = (esp_timer_get_time() - start_us) / 1000;
            ESP_LOGI(TAG, "All essential state received (%lld ms)", (long long)elapsed_ms);
            return ESP_OK;
        }

        if (status_cb && (!notified_waiting || state_changed)) {
            char buffer[MQTT_DP_STATUS_BUFFER_LEN];
            format_missing_state(buffer, sizeof(buffer), weather_ready, room_ready, hvac_ready);
            status_cb(buffer, ctx);
            notified_waiting = true;
        }

        if (state_changed) {
            prev_weather = weather_ready;
            prev_room = room_ready;
            prev_hvac = hvac_ready;
        }

        if ((elapsed - last_log) >= log_interval) {
            ESP_LOGI(TAG,
                     "still waiting for initial state (weather=%d room=%d hvac=%d)",
                     weather_ready,
                     room_ready,
                     hvac_ready);
            last_log = elapsed;
        }

        vTaskDelay(poll_interval);
        elapsed += poll_interval;
    }

    int64_t elapsed_ms = (esp_timer_get_time() - start_us) / 1000;
    ESP_LOGW(TAG,
             "Timeout waiting for initial state after %lld ms (weather=%d room=%d hvac=%d)",
             (long long)elapsed_ms,
             prev_weather,
             prev_room,
             prev_hvac);
    return ESP_ERR_TIMEOUT;
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
        msg.type = DP_MSG_FRAGMENT;
        msg.payload.fragment.msg_id = event->msg_id;
        msg.payload.fragment.total_len = (event->total_data_len > 0) ? event->total_data_len : event->data_len;
        msg.payload.fragment.fragment_len = event->data_len;
        msg.payload.fragment.offset = event->current_data_offset;
        msg.payload.fragment.timestamp_us = esp_timer_get_time();
        msg.payload.fragment.topic_len = event->topic_len;
        msg.payload.fragment.topic = NULL;
        msg.payload.fragment.data = NULL;
        msg.payload.fragment.retained = event->retain;
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

static void init_command_topic(void)
{
    if (s_command_topic_len > 0) {
        return;  // Already initialized
    }
    const char *theo_base = env_sensors_get_theo_base_topic();
    if (theo_base == NULL || theo_base[0] == '\0') {
        ESP_LOGW(TAG, "Theo base topic not available; command topic disabled");
        return;
    }
    int written = snprintf(s_command_topic, sizeof(s_command_topic), "%s/command", theo_base);
    if (written > 0 && written < (int)sizeof(s_command_topic)) {
        s_command_topic_len = (size_t)written;
        ESP_LOGI(TAG, "command topic: %s", s_command_topic);
    } else {
        ESP_LOGW(TAG, "command topic overflow");
        s_command_topic[0] = '\0';
        s_command_topic_len = 0;
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

    // Initialize and subscribe to command topic (uses Theo base, separate from HA topics)
    init_command_topic();
    if (s_command_topic_len > 0) {
        int msg_id = esp_mqtt_client_subscribe(client, s_command_topic, 0);
        if (msg_id < 0) {
            ESP_LOGW(TAG, "subscribe failed topic=%s", s_command_topic);
        } else {
            ESP_LOGI(TAG, "subscribed topic=%s msg_id=%d", s_command_topic, msg_id);
        }
    }
}

static void format_missing_state(char *buffer,
                                 size_t buffer_len,
                                 bool weather_ready,
                                 bool room_ready,
                                 bool hvac_ready)
{
    if (buffer_len == 0) {
        return;
    }

    buffer[0] = '\0';
    size_t offset = snprintf(buffer, buffer_len, "Waiting for: ");
    if (offset >= buffer_len) {
        buffer[buffer_len - 1] = '\0';
        return;
    }

    bool first = true;
    if (!weather_ready) {
        offset += snprintf(buffer + offset,
                           buffer_len - offset,
                           "%sweather",
                           first ? "" : ", ");
        first = false;
    }
    if (!room_ready) {
        offset += snprintf(buffer + offset,
                           buffer_len - offset,
                           "%sroom",
                           first ? "" : ", ");
        first = false;
    }
    if (!hvac_ready) {
        snprintf(buffer + offset,
                 buffer_len - offset,
                 "%sHVAC",
                 first ? "" : ", ");
    }
}

static void process_command(const char *payload, size_t payload_len)
{
    char buffer[64];
    size_t copy_len = (payload_len < sizeof(buffer) - 1) ? payload_len : (sizeof(buffer) - 1);
    memcpy(buffer, payload, copy_len);
    buffer[copy_len] = '\0';

    if (strcmp(buffer, "rainbow") == 0) {
        ESP_LOGI(TAG, "Received rainbow command");
        thermostat_led_status_trigger_rainbow();
    } else if (strcmp(buffer, "heatwave") == 0) {
        ESP_LOGI(TAG, "Received heatwave command");
        thermostat_led_status_trigger_heatwave();
    } else if (strcmp(buffer, "coolwave") == 0) {
        ESP_LOGI(TAG, "Received coolwave command");
        thermostat_led_status_trigger_coolwave();
    } else if (strcmp(buffer, "sparkle") == 0) {
        ESP_LOGI(TAG, "Received sparkle command");
        thermostat_led_status_trigger_sparkle();
    } else if (strcmp(buffer, "restart") == 0) {
        ESP_LOGI(TAG, "Received restart command");
        esp_restart();
    } else {
        ESP_LOGW(TAG, "Unknown command: %s", buffer);
    }
}

static bool is_command_topic(const char *topic, size_t topic_len)
{
    return s_command_topic_len > 0 &&
           topic_len == s_command_topic_len &&
           strncmp(topic, s_command_topic, topic_len) == 0;
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
        // Check command topic first (uses Theo base, not HA base)
        size_t topic_len = strlen(topic);
        if (is_command_topic(topic, topic_len)) {
            process_command(data, m->payload.fragment.fragment_len);
        } else {
            process_payload(match_topic(topic, topic_len), data, m->payload.fragment.fragment_len, m->payload.fragment.retained, m->payload.fragment.timestamp_us);
        }
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
    state->retain_flag = m->payload.fragment.retained;
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
        // Check command topic first (uses Theo base, not HA base)
        if (is_command_topic(state->topic, state->topic_len)) {
            process_command(state->buffer, state->total_len);
        } else {
            process_payload(match_topic(state->topic, state->topic_len), state->buffer, state->total_len, state->retain_flag, state->timestamp_us);
        }
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
            s_reassembly[i].retain_flag = false;
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

    for (size_t i = 0; i < sizeof(s_topics) / sizeof(s_topics[0]); ++i) {
        const char *suffix = s_topics[i].suffix != NULL ? s_topics[i].suffix : "";
        size_t base_len = strlen(base);
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
        memcpy(s_topics[i].topic + offset, base, base_len);
        offset += base_len;
        if (add_slash) {
            s_topics[i].topic[offset++] = '/';
            memcpy(s_topics[i].topic + offset, suffix, suffix_len);
            offset += suffix_len;
        }
        s_topics[i].topic[offset] = '\0';
        s_topics[i].topic_len = strlen(s_topics[i].topic);
    }

    s_topics_initialized = true;
}

static topic_desc_t *match_topic(const char *topic, size_t topic_len)
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
      {"breezy", &breezy},
      {"clear-day", &clear_day},
      {"clear-night", &clear_night},
      {"cloudy", &cloudy},
      {"dangerous-wind", &dangerous_wind},
      {"drizzle", &drizzle},
      {"flurries", &flurries},
      {"fog", &fog},
      {"haze", &haze},
      {"heavy-rain", &heavy_rain},
      {"heavy-sleet", &heavy_sleet},
      {"heavy-snow", &heavy_snow},
      {"light-rain", &light_rain},
      {"light-sleet", &light_sleet},
      {"light-snow", &light_snow},
      {"mist", &mist},
      {"mostly-clear-day", &mostly_clear_day},
      {"mostly-clear-night", &mostly_clear_night},
      {"mostly-cloudy-day", &mostly_cloudy_day},
      {"mostly-cloudy-night", &mostly_cloudy_night},
      {"partly-cloudy-day", &partly_cloudy_day},
      {"partly-cloudy-night", &partly_cloudy_night},
      {"possible-precipitation-day", &possible_precipitation_day},
      {"possible-precipitation-night", &possible_precipitation_night},
      {"possible-rain-day", &possible_rain_day},
      {"possible-rain-night", &possible_rain_night},
      {"possible-sleet-day", &possible_sleet_day},
      {"possible-sleet-night", &possible_sleet_night},
      {"possible-snow-day", &possible_snow_day},
      {"possible-snow-night", &possible_snow_night},
      {"possible-thunderstorm-day", &possible_thunderstorm_day},
      {"possible-thunderstorm-night", &possible_thunderstorm_night},
      {"precipitation", &precipitation},
      {"rain", &rain},
      {"sleet", &sleet},
      {"smoke", &smoke},
      {"snow", &snow},
      {"thunderstorm", &thunderstorm},
      {"very-light-sleet", &very_light_sleet},
      {"wind", &wind},
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

static void process_payload(topic_desc_t *desc, char *payload, size_t payload_len, bool retained, int64_t timestamp_us)
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
        if (!desc->seen) {
            ESP_LOGI(TAG, "initial weather temperature payload=%s", buffer);
        }
        if (esp_lv_adapter_lock(-1) == ESP_OK) {
            g_view_model.weather_ready = true;
            g_view_model.weather_temp_valid = ok;
            if (ok) {
                g_view_model.weather_temp_c = value;
            }
            if (g_ui_initialized) {
                thermostat_update_weather_group();
            }
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
        if (!desc->seen) {
            ESP_LOGI(TAG, "initial weather icon payload=%s", buffer);
        }
        if (esp_lv_adapter_lock(-1) == ESP_OK) {
            g_view_model.weather_ready = true;
            g_view_model.weather_icon = icon;
            if (g_ui_initialized) {
                thermostat_update_weather_group();
            }
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
        if (!desc->seen) {
            ESP_LOGI(TAG, "initial room temperature payload=%s", buffer);
        }
        if (esp_lv_adapter_lock(-1) == ESP_OK) {
            g_view_model.room_ready = true;
            g_view_model.room_temp_valid = ok;
            if (ok) {
                g_view_model.room_temp_c = value;
            }
            if (g_ui_initialized) {
                thermostat_update_room_group();
            }
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
        if (!desc->seen) {
            ESP_LOGI(TAG, "initial room name payload=%s", buffer[0] ? buffer : "(empty)");
        }
        if (esp_lv_adapter_lock(-1) == ESP_OK) {
            g_view_model.room_ready = true;
            g_view_model.room_icon = icon;
            g_view_model.room_icon_error = error;
            if (g_ui_initialized) {
                thermostat_update_room_group();
            }
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
        if (!desc->seen) {
            ESP_LOGI(TAG, "initial fan state payload=%s", buffer);
        }
        if (esp_lv_adapter_lock(-1) == ESP_OK) {
            if (ok) {
                g_view_model.fan_running = on;
                g_view_model.fan_payload_error = false;
            } else {
                g_view_model.fan_payload_error = true;
            }
            if (g_ui_initialized) {
                thermostat_update_action_bar_visuals();
            }
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
        if (!desc->seen) {
            const char *label = (desc->id == TOPIC_HEAT_STATE) ? "heat" : "cool";
            ESP_LOGI(TAG, "initial %s state payload=%s", label, buffer);
        }
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
            if (g_ui_initialized) {
                thermostat_update_hvac_status_group();
            }
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
        if (!desc->seen) {
            const char *label = (desc->id == TOPIC_SETPOINT_HIGH) ? "cooling" : "heating";
            ESP_LOGI(TAG, "initial %s setpoint payload=%s", label, buffer);
        }
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
                if (g_ui_initialized) {
                    thermostat_update_setpoint_labels();
                }
                esp_lv_adapter_unlock();
            }
            break;
        }
        if (clamped) {
            ESP_LOGW(TAG, "%s clamped to %.2f", desc->topic, value);
        }
        if (esp_lv_adapter_lock(-1) == ESP_OK) {
            if (g_ui_initialized) {
                thermostat_remote_setpoint_controller_submit(target, value);
            } else {
                // Store setpoint directly if UI not ready
                if (target == THERMOSTAT_TARGET_COOL) {
                    g_view_model.cooling_setpoint_c = value;
                    g_view_model.cooling_setpoint_valid = true;
                } else {
                    g_view_model.heating_setpoint_c = value;
                    g_view_model.heating_setpoint_valid = true;
                }
            }
            esp_lv_adapter_unlock();
        } else {
            ESP_LOGW(TAG, "LVGL lock timeout applying remote setpoint");
        }
        break;
    }
    case TOPIC_PERSONAL_FACE:
        if (!desc->seen) {
            ESP_LOGI(TAG, "initial face payload=%s", buffer);
        }
        thermostat_personal_presence_process_face(buffer, retained);
        break;
    case TOPIC_PERSONAL_COUNT:
        if (!desc->seen) {
            ESP_LOGI(TAG, "initial person count payload=%s", buffer);
        }
        thermostat_personal_presence_process_person_count(buffer);
        break;
    default:
        ESP_LOGW(TAG, "Unhandled topic id=%d", desc->id);
        break;
    }

    desc->seen = true;
    return;
}
