/**
 * @file radar_presence.c
 * @brief LD2410C mmWave radar presence detection implementation
 */

#include "sensors/radar_presence.h"

#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "mqtt_client.h"
#include "sdkconfig.h"

#include "ld2410.h"
#include "connectivity/mqtt_manager.h"
#include "sensors/env_sensors.h"

static const char *TAG = "radar_presence";

#define RADAR_TASK_STACK      (4096)
#define RADAR_TASK_PRIO       (4)
#define RADAR_TOPIC_MAX_LEN   (160)
#define RADAR_POLL_MS         (100)
#define RADAR_FRAME_TIMEOUT_US (1000000LL)  // 1 second

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
static bool s_online;
static uint8_t s_consecutive_failures;
static bool s_last_presence_published;
static uint16_t s_last_distance_published;

static void radar_task(void *arg);
static void build_topic(char *buf, size_t buf_len, radar_sensor_id_t sensor_id, const char *suffix);
static void publish_discovery_config(radar_sensor_id_t sensor_id);
static void publish_availability(radar_sensor_id_t sensor_id, bool online);
static void publish_presence_state(bool presence);
static void publish_distance_state(uint16_t distance_cm);
static void handle_frame_success(bool presence, uint16_t distance_cm);
static void handle_frame_timeout(void);

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

  // Create polling task
  BaseType_t task_ok = xTaskCreate(
      radar_task,
      "radar",
      RADAR_TASK_STACK,
      NULL,
      RADAR_TASK_PRIO,
      &s_task_handle);
  if (task_ok != pdPASS) {
    ESP_LOGE(TAG, "Failed to create radar task");
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

static void radar_task(void *arg)
{
  (void)arg;

  const TickType_t poll_interval = pdMS_TO_TICKS(RADAR_POLL_MS);
  const int64_t log_interval_us = CONFIG_THEO_SENSOR_POLL_SECONDS * 1000000LL;
  int64_t last_valid_frame_us = esp_timer_get_time();
  int64_t last_log_us = 0;

  ESP_LOGI(TAG, "Radar task started");

  while (true) {
    Response_t response = ld2410_check(s_radar_device);

    if (response == RP_DATA) {
      // Valid data frame received
      last_valid_frame_us = esp_timer_get_time();

      bool presence = ld2410_presence_detected(s_radar_device);
      uint16_t distance = (uint16_t)ld2410_detected_distance(s_radar_device);
      uint16_t moving_dist = (uint16_t)ld2410_moving_target_distance(s_radar_device);
      uint16_t still_dist = (uint16_t)ld2410_stationary_target_distance(s_radar_device);
      uint8_t moving_energy = ld2410_moving_target_signal(s_radar_device);
      uint8_t still_energy = ld2410_stationary_target_signal(s_radar_device);

      // Periodic measurement logging
      if ((last_valid_frame_us - last_log_us) >= log_interval_us) {
        ESP_LOGI(TAG, "LD2410: presence=%s, dist=%ucm, moving=%ucm/%u%%, still=%ucm/%u%%",
                 presence ? "yes" : "no",
                 distance,
                 moving_dist, moving_energy,
                 still_dist, still_energy);
        last_log_us = last_valid_frame_us;
      }

      // Update cached state under mutex
      if (xSemaphoreTake(s_state_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        s_cached_state.presence_detected = presence;
        s_cached_state.detection_distance_cm = distance;
        s_cached_state.moving_distance_cm = moving_dist;
        s_cached_state.still_distance_cm = still_dist;
        s_cached_state.moving_energy = moving_energy;
        s_cached_state.still_energy = still_energy;
        s_cached_state.last_update_us = last_valid_frame_us;
        xSemaphoreGive(s_state_mutex);
      }

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
  const char *theo_base = env_sensors_get_theo_base_topic();
  const char *slug = env_sensors_get_device_slug();
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

  const char *slug = env_sensors_get_device_slug();
  radar_sensor_meta_t *meta = &s_sensor_meta[sensor_id];

  // Build discovery topic: homeassistant/<sensor_type>/<slug>-theostat/<object_id>/config
  char discovery_topic[RADAR_TOPIC_MAX_LEN];
  snprintf(discovery_topic, sizeof(discovery_topic),
           "homeassistant/%s/%s-theostat/%s/config",
           meta->sensor_type, slug, meta->object_id);

  // Build state and availability topics
  char state_topic[RADAR_TOPIC_MAX_LEN];
  char avail_topic[RADAR_TOPIC_MAX_LEN];
  build_topic(state_topic, sizeof(state_topic), sensor_id, "state");
  build_topic(avail_topic, sizeof(avail_topic), sensor_id, "availability");

  // Build unique ID
  char unique_id[64];
  snprintf(unique_id, sizeof(unique_id), "theostat_%s_%s", slug, meta->object_id);

  // Build device name and ID
  const char *friendly_name = env_sensors_get_device_friendly_name();
  char device_name[80];
  char device_id[48];
  snprintf(device_name, sizeof(device_name), "%s Theostat", friendly_name);
  snprintf(device_id, sizeof(device_id), "theostat_%s", slug);

  // Build discovery payload
  char payload[768];
  int written;

  if (sensor_id == RADAR_SENSOR_PRESENCE) {
    // Binary sensor - no unit or state_class
    written = snprintf(payload, sizeof(payload),
        "{"
        "\"name\":\"%s\","
        "\"device_class\":\"%s\","
        "\"unique_id\":\"%s\","
        "\"state_topic\":\"%s\","
        "\"payload_on\":\"ON\","
        "\"payload_off\":\"OFF\","
        "\"availability_topic\":\"%s\","
        "\"payload_available\":\"online\","
        "\"payload_not_available\":\"offline\","
        "\"device\":{"
          "\"name\":\"%s\","
          "\"identifiers\":[\"%s\"],"
          "\"manufacturer\":\"Theo\","
          "\"model\":\"Theostat v1\""
        "}"
        "}",
        meta->name,
        meta->device_class,
        unique_id,
        state_topic,
        avail_topic,
        device_name,
        device_id);
  } else {
    // Regular sensor with unit
    written = snprintf(payload, sizeof(payload),
        "{"
        "\"name\":\"%s\","
        "\"device_class\":\"%s\","
        "\"state_class\":\"measurement\","
        "\"unit_of_measurement\":\"%s\","
        "\"unique_id\":\"%s\","
        "\"state_topic\":\"%s\","
        "\"availability_topic\":\"%s\","
        "\"payload_available\":\"online\","
        "\"payload_not_available\":\"offline\","
        "\"device\":{"
          "\"name\":\"%s\","
          "\"identifiers\":[\"%s\"],"
          "\"manufacturer\":\"Theo\","
          "\"model\":\"Theostat v1\""
        "}"
        "}",
        meta->name,
        meta->device_class,
        meta->unit,
        unique_id,
        state_topic,
        avail_topic,
        device_name,
        device_id);
  }

  if (written <= 0 || written >= (int)sizeof(payload)) {
    ESP_LOGE(TAG, "Discovery payload overflow for %s", meta->object_id);
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
