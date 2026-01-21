#include "connectivity/device_telemetry.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mqtt_client.h"
#include "driver/temperature_sensor.h"
#include "sdkconfig.h"

#include "connectivity/mqtt_manager.h"
#include "connectivity/ha_discovery.h"
#include "sensors/env_sensors.h"

static const char *TAG = "device_telemetry";

#define DEVICE_TELEMETRY_TASK_STACK   (8192)
#define DEVICE_TELEMETRY_TASK_PRIO    (4)
#define DEVICE_TELEMETRY_TOPIC_MAX_LEN (160)
#define DEVICE_TELEMETRY_DEVICE_TOPIC_MAX_LEN (256)
#define DEVICE_TELEMETRY_PAYLOAD_MAX_LEN (768)
#define DEVICE_TELEMETRY_TEMP_MIN_C   (-10.0f)
#define DEVICE_TELEMETRY_TEMP_MAX_C   (80.0f)

typedef enum {
  DEVICE_TELEM_TEMP = 0,
  DEVICE_TELEM_RSSI,
  DEVICE_TELEM_HEAP,
  DEVICE_TELEM_COUNT,
} device_telemetry_id_t;

typedef struct {
  const char *object_id;
  const char *name;
  const char *device_class;
  const char *state_class;
  const char *unit;
  const char *entity_category;
  bool discovery_published;
} device_telemetry_sensor_t;

static device_telemetry_sensor_t s_sensors[DEVICE_TELEM_COUNT] = {
    [DEVICE_TELEM_TEMP] = {
        .object_id = "chip_temperature",
        .name = "Chip Temperature",
        .device_class = "temperature",
        .state_class = "measurement",
        .unit = "°C",
        .entity_category = "diagnostic",
        .discovery_published = false,
    },
    [DEVICE_TELEM_RSSI] = {
        .object_id = "wifi_rssi",
        .name = "Wi-Fi RSSI",
        .device_class = "signal_strength",
        .state_class = "measurement",
        .unit = "dBm",
        .entity_category = "diagnostic",
        .discovery_published = false,
    },
    [DEVICE_TELEM_HEAP] = {
        .object_id = "free_heap",
        .name = "Free Heap",
        .state_class = "measurement",
        .unit = "B",
        .entity_category = "diagnostic",
        .discovery_published = false,
    },
};

static TaskHandle_t s_task_handle;
static bool s_started;
static temperature_sensor_handle_t s_temp_handle;
static bool s_temp_sensor_available;

static void device_telemetry_task(void *arg);
static void build_state_topic(char *buffer, size_t buffer_len, const char *object_id);
static bool publish_discovery(device_telemetry_sensor_t *sensor);
static void publish_state(device_telemetry_sensor_t *sensor, const char *payload);
static bool read_chip_temperature(float *out_value);

esp_err_t device_telemetry_start(void)
{
  if (s_started) {
    return ESP_OK;
  }
  s_started = true;

  const char *slug = env_sensors_get_device_slug();
  assert(slug != NULL && slug[0] != '\0');

  temperature_sensor_config_t temp_cfg = {
      .range_min = DEVICE_TELEMETRY_TEMP_MIN_C,
      .range_max = DEVICE_TELEMETRY_TEMP_MAX_C,
  };
  esp_err_t err = temperature_sensor_install(&temp_cfg, &s_temp_handle);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Temperature sensor init failed: %s", esp_err_to_name(err));
    s_temp_sensor_available = false;
  } else {
    s_temp_sensor_available = true;
  }

  BaseType_t task_ok = xTaskCreatePinnedToCoreWithCaps(
      device_telemetry_task,
      "device_diag",
      DEVICE_TELEMETRY_TASK_STACK,
      NULL,
      DEVICE_TELEMETRY_TASK_PRIO,
      &s_task_handle,
      tskNO_AFFINITY,
      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (task_ok != pdPASS) {
    if (s_temp_handle) {
      temperature_sensor_uninstall(s_temp_handle);
      s_temp_handle = NULL;
    }
    s_temp_sensor_available = false;
    ESP_LOGE(TAG, "Failed to create telemetry task");
    return ESP_ERR_NO_MEM;
  }

  ESP_LOGI(TAG, "Device telemetry started (poll interval: %d s)", CONFIG_THEO_DIAG_POLL_SECONDS);
  return ESP_OK;
}

static void device_telemetry_task(void *arg)
{
  (void)arg;

  const TickType_t poll_interval = pdMS_TO_TICKS(CONFIG_THEO_DIAG_POLL_SECONDS * 1000);

  while (true) {
    if (mqtt_manager_is_ready()) {
      float temp_c = 0.0f;
      if (read_chip_temperature(&temp_c)) {
        char payload[16];
        snprintf(payload, sizeof(payload), "%.2f", temp_c);
        publish_state(&s_sensors[DEVICE_TELEM_TEMP], payload);
      }

      int rssi = 0;
      esp_err_t rssi_err = esp_wifi_sta_get_rssi(&rssi);
      if (rssi_err == ESP_OK) {
        char payload[12];
        snprintf(payload, sizeof(payload), "%d", rssi);
        publish_state(&s_sensors[DEVICE_TELEM_RSSI], payload);
      } else {
        ESP_LOGW(TAG, "RSSI read skipped: %s", esp_err_to_name(rssi_err));
      }

      uint32_t free_heap = esp_get_free_heap_size();
      char payload[16];
      snprintf(payload, sizeof(payload), "%u", (unsigned)free_heap);
      publish_state(&s_sensors[DEVICE_TELEM_HEAP], payload);
    }

    vTaskDelay(poll_interval);
  }
}

static bool read_chip_temperature(float *out_value)
{
  if (!s_temp_sensor_available || s_temp_handle == NULL || out_value == NULL) {
    return false;
  }

  esp_err_t err = temperature_sensor_enable(s_temp_handle);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Temperature sensor enable failed: %s", esp_err_to_name(err));
    return false;
  }

  float temp = 0.0f;
  err = temperature_sensor_get_celsius(s_temp_handle, &temp);
  esp_err_t disable_err = temperature_sensor_disable(s_temp_handle);
  if (disable_err != ESP_OK) {
    ESP_LOGW(TAG, "Temperature sensor disable failed: %s", esp_err_to_name(disable_err));
  }

  if (err != ESP_OK || !isfinite(temp)) {
    ESP_LOGW(TAG, "Temperature read failed: %s", esp_err_to_name(err));
    return false;
  }

  *out_value = temp;
  return true;
}

static void build_state_topic(char *buffer, size_t buffer_len, const char *object_id)
{
  const char *base = env_sensors_get_theo_base_topic();
  const char *slug = env_sensors_get_device_slug();
  int written = snprintf(buffer, buffer_len, "%s/sensor/%s/%s/state", base, slug, object_id);
  if (written < 0 || (size_t)written >= buffer_len) {
    ESP_LOGW(TAG, "State topic truncated (%s)", object_id);
  }
}

static bool publish_discovery(device_telemetry_sensor_t *sensor)
{
  if (!mqtt_manager_is_ready() || sensor == NULL) {
    return false;
  }

  esp_mqtt_client_handle_t client = mqtt_manager_get_client();
  if (client == NULL) {
    return false;
  }

  const char *slug = env_sensors_get_device_slug();
  const char *friendly = env_sensors_get_device_friendly_name();
  const char *theo_base = env_sensors_get_theo_base_topic();

  char discovery_topic[DEVICE_TELEMETRY_TOPIC_MAX_LEN];
  ha_discovery_build_topic(discovery_topic, sizeof(discovery_topic), "sensor", slug,
                           sensor->object_id);

  char state_topic[DEVICE_TELEMETRY_TOPIC_MAX_LEN];
  build_state_topic(state_topic, sizeof(state_topic), sensor->object_id);

  char device_avail_topic[DEVICE_TELEMETRY_DEVICE_TOPIC_MAX_LEN];
  snprintf(device_avail_topic, sizeof(device_avail_topic), "%s/%s/availability", theo_base, slug);

  ha_discovery_entity_t entity = {
      .component = "sensor",
      .object_id = sensor->object_id,
      .name = sensor->name,
      .device_class = sensor->device_class,
      .state_class = sensor->state_class,
      .unit = sensor->unit,
      .entity_category = sensor->entity_category,
      .state_topic = state_topic,
      .availability_topic = device_avail_topic,
  };

  char payload[DEVICE_TELEMETRY_PAYLOAD_MAX_LEN];
  if (ha_discovery_build_payload(payload, sizeof(payload), &entity, slug, friendly) < 0) {
    return false;
  }

  int msg_id = esp_mqtt_client_publish(client, discovery_topic, payload, 0, 0, 1);
  if (msg_id < 0) {
    ESP_LOGE(TAG, "Failed to publish discovery for %s", sensor->object_id);
    return false;
  }

  ESP_LOGI(TAG, "Published discovery config for %s (msg_id=%d)", sensor->object_id, msg_id);
  sensor->discovery_published = true;
  return true;
}

static void publish_state(device_telemetry_sensor_t *sensor, const char *payload)
{
  if (!mqtt_manager_is_ready() || sensor == NULL) {
    return;
  }

  if (!sensor->discovery_published) {
    if (!publish_discovery(sensor)) {
      return;
    }
  }

  esp_mqtt_client_handle_t client = mqtt_manager_get_client();
  if (client == NULL) {
    return;
  }

  char topic[DEVICE_TELEMETRY_TOPIC_MAX_LEN];
  build_state_topic(topic, sizeof(topic), sensor->object_id);

  int msg_id = esp_mqtt_client_publish(client, topic, payload, 0, 0, 1);
  if (msg_id < 0) {
    ESP_LOGW(TAG, "Failed to publish state for %s", sensor->object_id);
  }
}
