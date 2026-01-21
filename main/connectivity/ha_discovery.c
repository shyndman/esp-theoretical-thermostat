#include "connectivity/ha_discovery.h"

#include <stdio.h>
#include <string.h>
#include "esp_log.h"

static const char *TAG = "ha_discovery";

void ha_discovery_build_topic(char *buf, size_t buf_len, const char *component, const char *slug,
                              const char *object_id)
{
  if (buf == NULL || buf_len == 0) {
    return;
  }
  snprintf(buf, buf_len, "homeassistant/%s/%s/%s/config", component, slug, object_id);
}

int ha_discovery_build_payload(char *buf, size_t buf_len, const ha_discovery_entity_t *entity,
                               const char *slug, const char *friendly_name)
{
  if (buf == NULL || buf_len == 0 || entity == NULL || slug == NULL || friendly_name == NULL) {
    return -1;
  }

  char extra[256] = {0};
  size_t offset = 0;

  if (entity->device_class != NULL) {
    offset += snprintf(extra + offset, sizeof(extra) - offset, ",\"device_class\":\"%s\"",
                       entity->device_class);
  }
  if (entity->state_class != NULL && offset < sizeof(extra)) {
    offset += snprintf(extra + offset, sizeof(extra) - offset, ",\"state_class\":\"%s\"",
                       entity->state_class);
  }
  if (entity->unit != NULL && offset < sizeof(extra)) {
    offset += snprintf(extra + offset, sizeof(extra) - offset, ",\"unit_of_measurement\":\"%s\"",
                       entity->unit);
  }
  if (entity->entity_category != NULL && offset < sizeof(extra)) {
    offset += snprintf(extra + offset, sizeof(extra) - offset, ",\"entity_category\":\"%s\"",
                       entity->entity_category);
  }
  if (entity->payload_on != NULL && offset < sizeof(extra)) {
    offset +=
        snprintf(extra + offset, sizeof(extra) - offset, ",\"payload_on\":\"%s\"", entity->payload_on);
  }
  if (entity->payload_off != NULL && offset < sizeof(extra)) {
    offset += snprintf(extra + offset, sizeof(extra) - offset, ",\"payload_off\":\"%s\"",
                       entity->payload_off);
  }

  if (offset >= sizeof(extra)) {
    ESP_LOGE(TAG, "Extra fields overflow for %s", entity->object_id);
    return -1;
  }

  // Handle availability
  char avail_block[512] = {0};
  if (entity->sensor_availability_topic != NULL) {
    snprintf(avail_block, sizeof(avail_block),
             ",\"availability_mode\":\"all\""
             ",\"availability\":["
             "{\"topic\":\"%s\",\"payload_available\":\"online\",\"payload_not_available\":\"offline\"},"
             "{\"topic\":\"%s\",\"payload_available\":\"online\",\"payload_not_available\":\"offline\"}"
             "]",
             entity->availability_topic, entity->sensor_availability_topic);
  } else {
    snprintf(avail_block, sizeof(avail_block),
             ",\"availability_topic\":\"%s\""
             ",\"payload_available\":\"online\""
             ",\"payload_not_available\":\"offline\"",
             entity->availability_topic);
  }

  int written = snprintf(buf, buf_len,
                         "{"
                         "\"name\":\"%s\""
                         "%s"
                         ",\"unique_id\":\"theostat_%s_%s\""
                         ",\"state_topic\":\"%s\""
                         "%s"
                         ",\"device\":{"
                         "\"name\":\"%s Theostat\""
                         ",\"identifiers\":[\"theostat_%s\"]"
                         ",\"manufacturer\":\"Theo\""
                         ",\"model\":\"Theostat v1\""
                         "}"
                         "}",
                         entity->name, extra, slug, entity->object_id, entity->state_topic,
                         avail_block, friendly_name, slug);

  if (written <= 0 || written >= (int)buf_len) {
    ESP_LOGE(TAG, "Discovery payload overflow for %s", entity->object_id);
    return -1;
  }

  return written;
}
