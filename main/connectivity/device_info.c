#include "connectivity/device_info.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"
#include "mqtt_client.h"
#include "sdkconfig.h"

#include "connectivity/mqtt_manager.h"
#include "connectivity/ha_discovery.h"
#include "sensors/env_sensors.h"

static const char *TAG = "device_info";

#define DEVICE_INFO_TOPIC_MAX_LEN   (160)
#define DEVICE_INFO_DEVICE_TOPIC_MAX_LEN (256)
#define DEVICE_INFO_PAYLOAD_MAX_LEN (768)
#define DEVICE_INFO_TIME_LEN        (40)

typedef struct {
  const char *object_id;
  const char *name;
  const char *device_class;
  const char *state_class;
  const char *unit;
  const char *entity_category;
} device_info_sensor_t;

static const device_info_sensor_t s_sensors[] = {
    {.object_id = "boot_time",
     .name = "Boot Time",
     .device_class = "timestamp",
     .entity_category = "diagnostic"},
    {.object_id = "reboot_reason", .name = "Reboot Reason", .entity_category = "diagnostic"},
};

static bool s_started;
static bool s_published;

static void device_info_publish(void);
static void device_info_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data);
static void build_state_topic(char *buffer, size_t buffer_len, const char *object_id);
static bool publish_discovery(const device_info_sensor_t *sensor);
static void publish_state(const char *object_id, const char *payload);
static const char *reset_reason_to_string(esp_reset_reason_t reason);
static void format_boot_time(char *buffer, size_t buffer_len);

esp_err_t device_info_start(void)
{
  if (s_started) {
    return ESP_OK;
  }
  s_started = true;

  const char *slug = env_sensors_get_device_slug();
  assert(slug != NULL && slug[0] != '\0');

  esp_mqtt_client_handle_t client = mqtt_manager_get_client();
  ESP_RETURN_ON_FALSE(client != NULL, ESP_ERR_INVALID_STATE, TAG, "MQTT client missing");

  esp_err_t err = esp_mqtt_client_register_event(client, MQTT_EVENT_CONNECTED, device_info_event_handler, NULL);
  ESP_RETURN_ON_ERROR(err, TAG, "register event failed");

  if (mqtt_manager_is_ready()) {
    device_info_publish();
  }

  return ESP_OK;
}

static void device_info_event_handler(void *handler_args,
                                      esp_event_base_t base,
                                      int32_t event_id,
                                      void *event_data)
{
  (void)handler_args;
  (void)base;
  (void)event_data;

  if (event_id == MQTT_EVENT_CONNECTED) {
    device_info_publish();
  }
}

static void device_info_publish(void)
{
  if (s_published || !mqtt_manager_is_ready()) {
    return;
  }

  esp_mqtt_client_handle_t client = mqtt_manager_get_client();
  if (client == NULL) {
    return;
  }

  bool discovery_ok = true;
  for (size_t i = 0; i < sizeof(s_sensors) / sizeof(s_sensors[0]); ++i) {
    if (!publish_discovery(&s_sensors[i])) {
      discovery_ok = false;
    }
  }
  if (!discovery_ok) {
    return;
  }

  char boot_time[DEVICE_INFO_TIME_LEN];
  format_boot_time(boot_time, sizeof(boot_time));
  publish_state("boot_time", boot_time);

  const char *reason = reset_reason_to_string(esp_reset_reason());
  publish_state("reboot_reason", reason);

  s_published = true;
  ESP_LOGI(TAG, "Boot diagnostics published");
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

static bool publish_discovery(const device_info_sensor_t *sensor)
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

  char discovery_topic[DEVICE_INFO_TOPIC_MAX_LEN];
  ha_discovery_build_topic(discovery_topic, sizeof(discovery_topic), "sensor", slug,
                           sensor->object_id);

  char state_topic[DEVICE_INFO_TOPIC_MAX_LEN];
  build_state_topic(state_topic, sizeof(state_topic), sensor->object_id);

  char device_avail_topic[DEVICE_INFO_DEVICE_TOPIC_MAX_LEN];
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

  char payload[DEVICE_INFO_PAYLOAD_MAX_LEN];
  if (ha_discovery_build_payload(payload, sizeof(payload), &entity, slug, friendly) < 0) {
    return false;
  }

  int msg_id = esp_mqtt_client_publish(client, discovery_topic, payload, 0, 0, 1);
  if (msg_id < 0) {
    ESP_LOGE(TAG, "Failed to publish discovery for %s", sensor->object_id);
    return false;
  }

  ESP_LOGI(TAG, "Published discovery config for %s (msg_id=%d)", sensor->object_id, msg_id);
  return true;
}

static void publish_state(const char *object_id, const char *payload)
{
  if (!mqtt_manager_is_ready()) {
    return;
  }

  esp_mqtt_client_handle_t client = mqtt_manager_get_client();
  if (client == NULL) {
    return;
  }

  char topic[DEVICE_INFO_TOPIC_MAX_LEN];
  build_state_topic(topic, sizeof(topic), object_id);

  int msg_id = esp_mqtt_client_publish(client, topic, payload, 0, 0, 1);
  if (msg_id < 0) {
    ESP_LOGW(TAG, "Failed to publish state for %s", object_id);
  }
}

static const char *reset_reason_to_string(esp_reset_reason_t reason)
{
  switch (reason) {
  case ESP_RST_POWERON:
    return "POWERON";
  case ESP_RST_EXT:
    return "EXT_RESET";
  case ESP_RST_SW:
    return "SW_RESET";
  case ESP_RST_PANIC:
    return "PANIC";
  case ESP_RST_INT_WDT:
    return "INT_WDT";
  case ESP_RST_TASK_WDT:
    return "TASK_WDT";
  case ESP_RST_WDT:
    return "WDT";
  case ESP_RST_DEEPSLEEP:
    return "DEEPSLEEP";
  case ESP_RST_BROWNOUT:
    return "BROWNOUT";
  case ESP_RST_SDIO:
    return "SDIO";
  case ESP_RST_UNKNOWN:
  default:
    return "UNKNOWN";
  }
}

static void format_boot_time(char *buffer, size_t buffer_len)
{
  if (buffer == NULL || buffer_len == 0) {
    return;
  }

  time_t now = time(NULL);
  struct tm local = {0};
  if (localtime_r(&now, &local) == NULL) {
    snprintf(buffer, buffer_len, "1970-01-01T00:00:00+0000");
    return;
  }

  size_t written = strftime(buffer, buffer_len, "%Y-%m-%dT%H:%M:%S%z", &local);
  if (written == 0) {
    snprintf(buffer, buffer_len, "1970-01-01T00:00:00+0000");
  }
}
