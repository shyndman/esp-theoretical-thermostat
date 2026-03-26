/**
 * @file radar_presence.c
 * @brief LD2410C mmWave radar presence detection implementation
 */

#include "sensors/radar_presence.h"

#include <string.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "mqtt_client.h"
#include "sdkconfig.h"

#include "ld2410.h"
#include "connectivity/mqtt_manager.h"
#include "connectivity/ha_discovery.h"
#include "connectivity/device_identity.h"

#ifndef MALLOC_CAP_SPIRAM
#define MALLOC_CAP_SPIRAM (1 << 10)
#endif
#ifndef MALLOC_CAP_8BIT
#define MALLOC_CAP_8BIT (1 << 2)
#endif

static const char *TAG = "radar_presence";

#ifdef CONFIG_THEO_RADAR_ENABLE

#define RADAR_TASK_STACK      (8192)
#define RADAR_TASK_PRIO       (4)
#define RADAR_TOPIC_MAX_LEN   (160)
#define RADAR_DEVICE_TOPIC_MAX_LEN (256)
#define RADAR_PAYLOAD_MAX_LEN (896)
#define RADAR_POLL_MS         (100)
#define RADAR_FRAME_TIMEOUT_US (1000000LL)  // 1 second
#define RADAR_STATE_MUTEX_TIMEOUT_MS (50)
#define RADAR_GATE_COUNT      (9)
#define RADAR_ENERGY_UNAVAILABLE (-1)

typedef enum {
  RADAR_SENSOR_PRESENCE = 0,
  RADAR_SENSOR_DISTANCE,
  RADAR_SENSOR_COUNT,
} radar_sensor_id_t;

typedef struct {
  const char *object_id;
  const char *name;
  const char *device_class;
  const char *unit;
  const char *sensor_type;  // "sensor" or "binary_sensor"
  bool online;
  bool discovery_published;
} radar_sensor_meta_t;

static radar_sensor_meta_t s_sensor_meta[RADAR_SENSOR_COUNT] = {
    [RADAR_SENSOR_PRESENCE] = {
        .object_id = "radar_presence",
        .name = "Radar Presence",
        .device_class = "occupancy",
        .unit = NULL,
        .sensor_type = "binary_sensor",
        .online = false,
        .discovery_published = false,
    },
    [RADAR_SENSOR_DISTANCE] = {
        .object_id = "radar_distance",
        .name = "Detection Distance",
        .device_class = "distance",
        .unit = "cm",
        .sensor_type = "sensor",
        .online = false,
        .discovery_published = false,
    },
};

static LD2410_device_t *s_radar_device;
static TaskHandle_t s_task_handle;
static SemaphoreHandle_t s_state_mutex;
static radar_presence_state_t s_cached_state;
static bool s_started;
static bool s_mqtt_event_registered;
static bool s_online;
static uint8_t s_consecutive_failures;
static bool s_last_presence_published;
static uint16_t s_last_distance_published;

static void radar_task(void *arg);
static void radar_mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data);
static void build_topic(char *buf, size_t buf_len, radar_sensor_id_t sensor_id, const char *suffix);
static void republish_mqtt_state(void);
static void publish_discovery_config(radar_sensor_id_t sensor_id);
static void publish_availability(radar_sensor_id_t sensor_id, bool online);
static void publish_presence_state(bool presence);
static void publish_distance_state(uint16_t distance_cm);
static void handle_frame_success(bool presence, uint16_t distance_cm);
static void handle_frame_timeout(void);
static int radar_signal_value_for_gate(ValuesArray_t values, bool available, uint8_t gate);
static int radar_threshold_value_for_gate(ValuesArray_t values, uint8_t gate);

esp_err_t radar_presence_start(void)
{
  if (s_started) {
    return ESP_OK;
  }

  ESP_LOGI(TAG, "Initializing LD2410C radar presence sensor");

  // Create state mutex
  s_state_mutex = xSemaphoreCreateMutex();
  ESP_RETURN_ON_FALSE(s_state_mutex != NULL, ESP_ERR_NO_MEM, TAG, "Failed to create state mutex");

  // Create LD2410 device handle
  s_radar_device = ld2410_new();
  if (s_radar_device == NULL) {
    ESP_LOGE(TAG, "Failed to allocate LD2410 device");
    vSemaphoreDelete(s_state_mutex);
    s_state_mutex = NULL;
    return ESP_ERR_NO_MEM;
  }

  // Initialize UART and check radar response
  if (!ld2410_begin(s_radar_device)) {
    ESP_LOGE(TAG, "LD2410 UART init failed or radar not responding");
    ld2410_free(s_radar_device);
    s_radar_device = NULL;
    vSemaphoreDelete(s_state_mutex);
    s_state_mutex = NULL;
    return ESP_ERR_NOT_FOUND;
  }

  ESP_LOGI(TAG, "LD2410C radar initialized (UART%d, RX=%d, TX=%d, baud=%d)",
           CONFIG_LD2410_UART_PORT_NUM,
           CONFIG_LD2410_UART_RX,
           CONFIG_LD2410_UART_TX,
           CONFIG_LD2410_UART_BAUD_RATE);

  esp_mqtt_client_handle_t client = mqtt_manager_get_client();
  if (client == NULL) {
    ESP_LOGE(TAG, "MQTT client missing");
    ld2410_free(s_radar_device);
    s_radar_device = NULL;
    vSemaphoreDelete(s_state_mutex);
    s_state_mutex = NULL;
    return ESP_ERR_INVALID_STATE;
  }

  esp_err_t err = esp_mqtt_client_register_event(client, MQTT_EVENT_CONNECTED, radar_mqtt_event_handler, NULL);
  if (err != ESP_OK) {
    ld2410_free(s_radar_device);
    s_radar_device = NULL;
    vSemaphoreDelete(s_state_mutex);
    s_state_mutex = NULL;
    return err;
  }
  s_mqtt_event_registered = true;

  if (mqtt_manager_is_ready()) {
    republish_mqtt_state();
  }

  // Create polling task
  BaseType_t task_ok = xTaskCreatePinnedToCoreWithCaps(
      radar_task,
      "radar",
      RADAR_TASK_STACK,
      NULL,
      RADAR_TASK_PRIO,
      &s_task_handle,
      tskNO_AFFINITY,
      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (task_ok != pdPASS) {
    ESP_LOGE(TAG, "Failed to create radar task");
    esp_mqtt_client_unregister_event(client, MQTT_EVENT_CONNECTED, radar_mqtt_event_handler);
    s_mqtt_event_registered = false;
    ld2410_free(s_radar_device);
    s_radar_device = NULL;
    vSemaphoreDelete(s_state_mutex);
    s_state_mutex = NULL;
    return ESP_ERR_NO_MEM;
  }

  s_started = true;
  ESP_LOGI(TAG, "Radar presence sensor started (poll: %d ms, wake distance: %d cm, dwell: %d ms)",
           CONFIG_THEO_RADAR_POLL_INTERVAL_MS,
           CONFIG_THEO_RADAR_WAKE_DISTANCE_CM,
           CONFIG_THEO_RADAR_WAKE_DWELL_MS);
  return ESP_OK;
}

esp_err_t radar_presence_stop(void)
{
  if (!s_started) {
    return ESP_OK;
  }

  if (s_task_handle != NULL) {
    vTaskDelete(s_task_handle);
    s_task_handle = NULL;
  }

  if (s_mqtt_event_registered) {
    esp_mqtt_client_handle_t client = mqtt_manager_get_client();
    if (client != NULL) {
      esp_mqtt_client_unregister_event(client, MQTT_EVENT_CONNECTED, radar_mqtt_event_handler);
    }
    s_mqtt_event_registered = false;
  }

  if (s_radar_device != NULL) {
    ld2410_free(s_radar_device);
    s_radar_device = NULL;
  }

  if (s_state_mutex != NULL) {
    vSemaphoreDelete(s_state_mutex);
    s_state_mutex = NULL;
  }

  s_started = false;
  s_online = false;
  s_consecutive_failures = 0;
  for (int i = 0; i < RADAR_SENSOR_COUNT; ++i) {
    s_sensor_meta[i].online = false;
  }
  ESP_LOGI(TAG, "Radar presence sensor stopped");
  return ESP_OK;
}

bool radar_presence_get_state(radar_presence_state_t *out)
{
  if (out == NULL || s_state_mutex == NULL) {
    return false;
  }

  if (xSemaphoreTake(s_state_mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
    return false;
  }

  memcpy(out, &s_cached_state, sizeof(radar_presence_state_t));
  bool online = s_online;
  xSemaphoreGive(s_state_mutex);

  return online;
}

bool radar_presence_is_online(void)
{
  return s_online;
}

esp_err_t radar_presence_dump_thresholds(void)
{
  if (!s_started || s_radar_device == NULL || s_state_mutex == NULL) {
    return ESP_ERR_INVALID_STATE;
  }

  if (xSemaphoreTake(s_state_mutex, pdMS_TO_TICKS(RADAR_STATE_MUTEX_TIMEOUT_MS)) != pdTRUE) {
    return ESP_ERR_TIMEOUT;
  }

  esp_err_t result = ESP_OK;
  uint8_t resolution_cm = 0;
  ValuesArray_t moving_thresholds = {0};
  ValuesArray_t stationary_thresholds = {0};
  uint8_t max_moving_gate = 0;
  uint8_t max_stationary_gate = 0;
  uint8_t no_one_window_s = 0;
  ValuesArray_t moving_signals = {0};
  ValuesArray_t stationary_signals = {0};
  bool enhanced = false;
  int64_t frame_age_ms = -1;

  if (!s_online) {
    result = ESP_ERR_INVALID_STATE;
  } else {
    SensorData_t sensor_data = ld2410_get_sensor_data(s_radar_device);
    resolution_cm = ld2410_get_resolution(s_radar_device);
    moving_thresholds = ld2410_get_moving_thresholds(s_radar_device);
    stationary_thresholds = ld2410_get_stationary_thresholds(s_radar_device);
    max_moving_gate = ld2410_get_max_moving_gate(s_radar_device);
    max_stationary_gate = ld2410_get_max_stationary_gate(s_radar_device);
    no_one_window_s = ld2410_get_no_one_window(s_radar_device);
    moving_signals = ld2410_get_moving_signals(s_radar_device);
    stationary_signals = ld2410_get_stationary_signals(s_radar_device);
    enhanced = s_radar_device->isEnhanced;

    if (sensor_data.timestamp_ms > 0) {
      int64_t now_ms = esp_timer_get_time() / 1000;
      frame_age_ms = now_ms >= sensor_data.timestamp_ms ? (now_ms - sensor_data.timestamp_ms) : 0;
    }
  }

  xSemaphoreGive(s_state_mutex);

  if (result != ESP_OK) {
    return result;
  }

  ESP_LOGI(TAG, "LD2410 threshold dump");
  ESP_LOGI(TAG,
           "Context: resolution=%ucm, max_moving_gate=%u, max_still_gate=%u, no_one_window_s=%u, enhanced=%s, frame_age_ms=%lld",
           resolution_cm,
           max_moving_gate,
           max_stationary_gate,
           no_one_window_s,
           enhanced ? "yes" : "no",
           (long long)frame_age_ms);
  ESP_LOGI(TAG,
           "gate,kind,range_start_cm,range_end_cm,moving_threshold,still_threshold,moving_energy,still_energy");

  for (uint8_t gate = 0; gate < RADAR_GATE_COUNT; ++gate) {
    const char *kind = gate == 0 ? "near_field_special" : "normal";
    int range_start_cm = gate == 0 ? 0 : (int)(gate - 1) * resolution_cm;
    int range_end_cm = gate == 0 ? 0 : (int)gate * resolution_cm;

    ESP_LOGI(TAG,
             "%u,%s,%d,%d,%d,%d,%d,%d",
             gate,
             kind,
             range_start_cm,
             range_end_cm,
             radar_threshold_value_for_gate(moving_thresholds, gate),
             radar_threshold_value_for_gate(stationary_thresholds, gate),
             radar_signal_value_for_gate(moving_signals, enhanced, gate),
             radar_signal_value_for_gate(stationary_signals, enhanced, gate));
  }

  return ESP_OK;
}

esp_err_t radar_presence_start_calibration(void)
{
  if (!s_started || s_radar_device == NULL || s_state_mutex == NULL) {
    return ESP_ERR_INVALID_STATE;
  }

  if (xSemaphoreTake(s_state_mutex, pdMS_TO_TICKS(RADAR_STATE_MUTEX_TIMEOUT_MS)) != pdTRUE) {
    return ESP_ERR_TIMEOUT;
  }

  esp_err_t result = ESP_OK;
  if (!s_online) {
    result = ESP_ERR_INVALID_STATE;
  } else if (!ld2410_auto_thresholds(s_radar_device, 10)) {
    result = ESP_FAIL;
  }

  xSemaphoreGive(s_state_mutex);
  return result;
}

static void radar_mqtt_event_handler(void *handler_args,
                                     esp_event_base_t base,
                                     int32_t event_id,
                                     void *event_data)
{
  (void)handler_args;
  (void)base;
  (void)event_data;

  if (event_id == MQTT_EVENT_CONNECTED) {
    republish_mqtt_state();
  }
}

static void republish_mqtt_state(void)
{
  radar_presence_state_t cached_state = {0};
  bool has_cached_state = false;

  if (s_state_mutex != NULL && xSemaphoreTake(s_state_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
    cached_state = s_cached_state;
    has_cached_state = s_online && cached_state.last_update_us > 0;
    xSemaphoreGive(s_state_mutex);
  }

  for (int i = 0; i < RADAR_SENSOR_COUNT; ++i) {
    publish_discovery_config((radar_sensor_id_t)i);
  }

  for (int i = 0; i < RADAR_SENSOR_COUNT; ++i) {
    publish_availability((radar_sensor_id_t)i, s_sensor_meta[i].online);
  }

  if (!has_cached_state) {
    return;
  }

  publish_presence_state(cached_state.presence_detected);
  publish_distance_state(cached_state.detection_distance_cm);
}

static void radar_task(void *arg)
{
  (void)arg;

  const TickType_t poll_interval = pdMS_TO_TICKS(RADAR_POLL_MS);
  const int64_t log_interval_us = CONFIG_THEO_SENSOR_POLL_SECONDS * 1000000LL;
  int64_t last_valid_frame_us = esp_timer_get_time();
  int64_t last_log_us = 0;

  ESP_LOGI(TAG, "Radar task started");

  while (true) {
    bool have_frame = false;
    bool presence = false;
    uint16_t distance = 0;
    uint16_t moving_dist = 0;
    uint16_t still_dist = 0;
    uint8_t moving_energy = 0;
    uint8_t still_energy = 0;

    if (xSemaphoreTake(s_state_mutex, pdMS_TO_TICKS(RADAR_STATE_MUTEX_TIMEOUT_MS)) == pdTRUE) {
      Response_t response = ld2410_check(s_radar_device);

      if (response == RP_DATA) {
        have_frame = true;
        last_valid_frame_us = esp_timer_get_time();

        presence = ld2410_presence_detected(s_radar_device);
        distance = (uint16_t)ld2410_detected_distance(s_radar_device);
        moving_dist = (uint16_t)ld2410_moving_target_distance(s_radar_device);
        still_dist = (uint16_t)ld2410_stationary_target_distance(s_radar_device);
        moving_energy = ld2410_moving_target_signal(s_radar_device);
        still_energy = ld2410_stationary_target_signal(s_radar_device);

        // Periodic measurement logging
        if ((last_valid_frame_us - last_log_us) >= log_interval_us) {
          ESP_LOGI(TAG, "LD2410: presence=%s, dist=%ucm, moving=%ucm/%u%%, still=%ucm/%u%%",
                   presence ? "yes" : "no",
                   distance,
                   moving_dist, moving_energy,
                   still_dist, still_energy);
          last_log_us = last_valid_frame_us;
        }

        s_cached_state.presence_detected = presence;
        s_cached_state.detection_distance_cm = distance;
        s_cached_state.moving_distance_cm = moving_dist;
        s_cached_state.still_distance_cm = still_dist;
        s_cached_state.moving_energy = moving_energy;
        s_cached_state.still_energy = still_energy;
        s_cached_state.last_update_us = last_valid_frame_us;
      }

      xSemaphoreGive(s_state_mutex);
    } else {
      ESP_LOGW(TAG, "Radar poll skipped: state mutex busy");
    }

    if (have_frame) {
      handle_frame_success(presence, distance);
    } else {
      // Check for frame timeout
      int64_t now = esp_timer_get_time();
      if ((now - last_valid_frame_us) > RADAR_FRAME_TIMEOUT_US) {
        handle_frame_timeout();
        last_valid_frame_us = now;  // Reset to prevent spamming
      }
    }

    vTaskDelay(poll_interval);
  }
}

static void build_topic(char *buf, size_t buf_len, radar_sensor_id_t sensor_id, const char *suffix)
{
  const char *theo_base = device_identity_get_theo_base_topic();
  const char *slug = device_identity_get_slug();
  const radar_sensor_meta_t *meta = &s_sensor_meta[sensor_id];

  int written = snprintf(buf, buf_len, "%s/%s/%s-theostat/%s/%s",
                         theo_base,
                         meta->sensor_type,
                         slug,
                         meta->object_id,
                         suffix);
  if (written < 0 || (size_t)written >= buf_len) {
    ESP_LOGW(TAG, "Topic truncated for %s/%s", meta->object_id, suffix);
  }
}

static void publish_discovery_config(radar_sensor_id_t sensor_id)
{
  if (!mqtt_manager_is_ready()) {
    return;
  }

  esp_mqtt_client_handle_t client = mqtt_manager_get_client();
  if (client == NULL) {
    return;
  }

  const char *slug = device_identity_get_slug();
  radar_sensor_meta_t *meta = &s_sensor_meta[sensor_id];

  // Build discovery topic
  char discovery_topic[RADAR_TOPIC_MAX_LEN];
  char node_id[64];
  snprintf(node_id, sizeof(node_id), "%s-theostat", slug);
  ha_discovery_build_topic(discovery_topic, sizeof(discovery_topic), meta->sensor_type, node_id,
                           meta->object_id);

  // Build state and availability topics
  char state_topic[RADAR_TOPIC_MAX_LEN];
  char avail_topic[RADAR_TOPIC_MAX_LEN];
  char device_avail_topic[RADAR_DEVICE_TOPIC_MAX_LEN];
  build_topic(state_topic, sizeof(state_topic), sensor_id, "state");
  build_topic(avail_topic, sizeof(avail_topic), sensor_id, "availability");
  snprintf(device_avail_topic, sizeof(device_avail_topic), "%s/%s/availability",
           device_identity_get_theo_base_topic(), slug);

  ha_discovery_entity_t entity = {
      .component = meta->sensor_type,
      .object_id = meta->object_id,
      .name = meta->name,
      .device_class = meta->device_class,
      .unit = meta->unit,
      .state_topic = state_topic,
      .availability_topic = device_avail_topic,
      .sensor_availability_topic = avail_topic,
  };

  if (sensor_id == RADAR_SENSOR_PRESENCE) {
    entity.payload_on = "ON";
    entity.payload_off = "OFF";
  } else {
    entity.state_class = "measurement";
  }

  char payload[RADAR_PAYLOAD_MAX_LEN];
  if (ha_discovery_build_payload(payload, sizeof(payload), &entity, slug,
                                 device_identity_get_friendly_name()) < 0) {
    return;
  }

  int msg_id = esp_mqtt_client_publish(client, discovery_topic, payload, 0, 0, 1);
  if (msg_id < 0) {
    ESP_LOGE(TAG, "Failed to publish discovery for %s", meta->object_id);
  } else {
    ESP_LOGI(TAG, "Published discovery config for %s (msg_id=%d)", meta->object_id, msg_id);
    meta->discovery_published = true;
  }
}

static void publish_availability(radar_sensor_id_t sensor_id, bool online)
{
  if (!mqtt_manager_is_ready()) {
    return;
  }

  esp_mqtt_client_handle_t client = mqtt_manager_get_client();
  if (client == NULL) {
    return;
  }

  char topic[RADAR_TOPIC_MAX_LEN];
  build_topic(topic, sizeof(topic), sensor_id, "availability");

  const char *payload = online ? "online" : "offline";
  int msg_id = esp_mqtt_client_publish(client, topic, payload, 0, 0, 1);
  if (msg_id < 0) {
    ESP_LOGW(TAG, "Failed to publish availability for %s", s_sensor_meta[sensor_id].object_id);
  } else {
    ESP_LOGI(TAG, "Published %s availability: %s", s_sensor_meta[sensor_id].object_id, payload);
  }
}

static void publish_presence_state(bool presence)
{
  if (!mqtt_manager_is_ready()) {
    return;
  }

  esp_mqtt_client_handle_t client = mqtt_manager_get_client();
  if (client == NULL) {
    return;
  }

  char topic[RADAR_TOPIC_MAX_LEN];
  build_topic(topic, sizeof(topic), RADAR_SENSOR_PRESENCE, "state");

  const char *payload = presence ? "ON" : "OFF";
  int msg_id = esp_mqtt_client_publish(client, topic, payload, 0, 0, 1);
  if (msg_id < 0) {
    ESP_LOGW(TAG, "Failed to publish presence state");
  }
}

static void publish_distance_state(uint16_t distance_cm)
{
  if (!mqtt_manager_is_ready()) {
    return;
  }

  esp_mqtt_client_handle_t client = mqtt_manager_get_client();
  if (client == NULL) {
    return;
  }

  char topic[RADAR_TOPIC_MAX_LEN];
  build_topic(topic, sizeof(topic), RADAR_SENSOR_DISTANCE, "state");

  char payload[16];
  snprintf(payload, sizeof(payload), "%u", distance_cm);

  int msg_id = esp_mqtt_client_publish(client, topic, payload, 0, 0, 1);
  if (msg_id < 0) {
    ESP_LOGW(TAG, "Failed to publish distance state");
  }
}

static void handle_frame_success(bool presence, uint16_t distance_cm)
{
  bool was_offline = !s_online;

  s_consecutive_failures = 0;

  // Publish discovery configs if not yet done
  for (int i = 0; i < RADAR_SENSOR_COUNT; ++i) {
    if (!s_sensor_meta[i].discovery_published) {
      publish_discovery_config((radar_sensor_id_t)i);
    }
  }

  // Transition to online if needed
  if (was_offline) {
    s_online = true;
    for (int i = 0; i < RADAR_SENSOR_COUNT; ++i) {
      s_sensor_meta[i].online = true;
      publish_availability((radar_sensor_id_t)i, true);
    }
    ESP_LOGI(TAG, "Radar now online");
  }

  // Publish state if changed (avoid flooding MQTT)
  if (presence != s_last_presence_published) {
    publish_presence_state(presence);
    s_last_presence_published = presence;
  }

  // Publish distance if presence detected and distance changed significantly (>5cm)
  if (presence) {
    int16_t delta = (int16_t)distance_cm - (int16_t)s_last_distance_published;
    if (delta < -5 || delta > 5 || s_last_distance_published == 0) {
      publish_distance_state(distance_cm);
      s_last_distance_published = distance_cm;
    }
  }
}

static void handle_frame_timeout(void)
{
  s_consecutive_failures++;

  ESP_LOGW(TAG, "Radar frame timeout (%d/%d)",
           s_consecutive_failures,
           CONFIG_THEO_RADAR_FAIL_THRESHOLD);

  if (s_online && s_consecutive_failures >= CONFIG_THEO_RADAR_FAIL_THRESHOLD) {
    s_online = false;
    for (int i = 0; i < RADAR_SENSOR_COUNT; ++i) {
      s_sensor_meta[i].online = false;
      publish_availability((radar_sensor_id_t)i, false);
    }
    ESP_LOGW(TAG, "Radar marked offline after %d consecutive timeouts",
             CONFIG_THEO_RADAR_FAIL_THRESHOLD);
  }
}

static int radar_signal_value_for_gate(ValuesArray_t values, bool available, uint8_t gate)
{
  if (!available || gate > values.N) {
    return RADAR_ENERGY_UNAVAILABLE;
  }

  return values.values[gate];
}

static int radar_threshold_value_for_gate(ValuesArray_t values, uint8_t gate)
{
  if (gate > values.N) {
    return RADAR_ENERGY_UNAVAILABLE;
  }

  return values.values[gate];
}

#else  // CONFIG_THEO_RADAR_ENABLE

esp_err_t radar_presence_start(void)
{
  ESP_LOGI(TAG, "Radar presence disabled via CONFIG_THEO_RADAR_ENABLE; skipping init");
  return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t radar_presence_stop(void)
{
  return ESP_OK;
}

esp_err_t radar_presence_dump_thresholds(void)
{
  return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t radar_presence_start_calibration(void)
{
  return ESP_ERR_NOT_SUPPORTED;
}

bool radar_presence_get_state(radar_presence_state_t *out)
{
  if (out != NULL)
  {
    memset(out, 0, sizeof(*out));
  }
  return false;
}

bool radar_presence_is_online(void)
{
  return false;
}

#endif  // CONFIG_THEO_RADAR_ENABLE
