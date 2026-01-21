#include "connectivity/device_ip_publisher.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "mqtt_client.h"
#include "sdkconfig.h"

#include "connectivity/mqtt_manager.h"
#include "connectivity/ha_discovery.h"
#include "sensors/env_sensors.h"

static const char *TAG = "device_ip";

#define DEVICE_IP_TOPIC_MAX_LEN   (160)
#define DEVICE_IP_DEVICE_TOPIC_MAX_LEN (256)
#define DEVICE_IP_PAYLOAD_MAX_LEN (640)
#define DEVICE_IP_ADDR_LEN        (16)

static bool s_started;
static bool s_discovery_published;

static void device_ip_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data);
static void device_ip_publish(void);
static bool publish_discovery(void);
static void publish_state(const char *ip_address);
static void build_state_topic(char *buffer, size_t buffer_len);

esp_err_t device_ip_publisher_start(void)
{
  if (s_started) {
    return ESP_OK;
  }
  s_started = true;

  const char *slug = env_sensors_get_device_slug();
  assert(slug != NULL && slug[0] != '\0');

  esp_mqtt_client_handle_t client = mqtt_manager_get_client();
  ESP_RETURN_ON_FALSE(client != NULL, ESP_ERR_INVALID_STATE, TAG, "MQTT client missing");

  esp_err_t err = esp_mqtt_client_register_event(client, MQTT_EVENT_CONNECTED, device_ip_event_handler, NULL);
  ESP_RETURN_ON_ERROR(err, TAG, "register event failed");

  if (mqtt_manager_is_ready()) {
    device_ip_publish();
  }

  return ESP_OK;
}

static void device_ip_event_handler(void *handler_args,
                                    esp_event_base_t base,
                                    int32_t event_id,
                                    void *event_data)
{
  (void)handler_args;
  (void)base;
  (void)event_data;

  if (event_id == MQTT_EVENT_CONNECTED) {
    device_ip_publish();
  }
}

static void device_ip_publish(void)
{
  if (!mqtt_manager_is_ready()) {
    return;
  }

  if (!s_discovery_published) {
    if (!publish_discovery()) {
      return;
    }
  }

  esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
  if (netif == NULL) {
    ESP_LOGW(TAG, "Wi-Fi STA netif not ready");
    return;
  }

  esp_netif_ip_info_t info = {0};
  esp_err_t err = esp_netif_get_ip_info(netif, &info);
  if (err != ESP_OK || info.ip.addr == 0) {
    ESP_LOGW(TAG, "STA IP not ready yet");
    return;
  }

  char ip_address[DEVICE_IP_ADDR_LEN];
  esp_ip4addr_ntoa(&info.ip, ip_address, sizeof(ip_address));
  publish_state(ip_address);
}

static void build_state_topic(char *buffer, size_t buffer_len)
{
  const char *base = env_sensors_get_theo_base_topic();
  const char *slug = env_sensors_get_device_slug();
  int written = snprintf(buffer, buffer_len, "%s/sensor/%s/ip_address/state", base, slug);
  if (written < 0 || (size_t)written >= buffer_len) {
    ESP_LOGW(TAG, "State topic truncated (ip_address)");
  }
}

static bool publish_discovery(void)
{
  if (!mqtt_manager_is_ready()) {
    return false;
  }

  esp_mqtt_client_handle_t client = mqtt_manager_get_client();
  if (client == NULL) {
    return false;
  }

  const char *slug = env_sensors_get_device_slug();
  const char *friendly = env_sensors_get_device_friendly_name();
  const char *theo_base = env_sensors_get_theo_base_topic();

  char discovery_topic[DEVICE_IP_TOPIC_MAX_LEN];
  ha_discovery_build_topic(discovery_topic, sizeof(discovery_topic), "sensor", slug, "ip_address");

  char state_topic[DEVICE_IP_TOPIC_MAX_LEN];
  build_state_topic(state_topic, sizeof(state_topic));

  char device_avail_topic[DEVICE_IP_DEVICE_TOPIC_MAX_LEN];
  snprintf(device_avail_topic, sizeof(device_avail_topic), "%s/%s/availability", theo_base, slug);

  ha_discovery_entity_t entity = {
      .component = "sensor",
      .object_id = "ip_address",
      .name = "IP Address",
      .device_class = NULL,
      .entity_category = "diagnostic",
      .state_topic = state_topic,
      .availability_topic = device_avail_topic,
  };

  char payload[DEVICE_IP_PAYLOAD_MAX_LEN];
  if (ha_discovery_build_payload(payload, sizeof(payload), &entity, slug, friendly) < 0) {
    return false;
  }

  int msg_id = esp_mqtt_client_publish(client, discovery_topic, payload, 0, 0, 1);
  if (msg_id < 0) {
    ESP_LOGE(TAG, "Failed to publish discovery for ip_address");
    return false;
  }

  s_discovery_published = true;
  ESP_LOGI(TAG, "Published discovery config for ip_address (msg_id=%d)", msg_id);
  return true;
}

static void publish_state(const char *ip_address)
{
  if (!mqtt_manager_is_ready()) {
    return;
  }

  esp_mqtt_client_handle_t client = mqtt_manager_get_client();
  if (client == NULL) {
    return;
  }

  char topic[DEVICE_IP_TOPIC_MAX_LEN];
  build_state_topic(topic, sizeof(topic));

  int msg_id = esp_mqtt_client_publish(client, topic, ip_address, 0, 0, 1);
  if (msg_id < 0) {
    ESP_LOGW(TAG, "Failed to publish IP state");
  }
}
