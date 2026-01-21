#ifndef HA_DISCOVERY_H
#define HA_DISCOVERY_H

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  const char *component;    // "sensor", "binary_sensor", etc.
  const char *object_id;
  const char *name;
  const char *device_class; // optional
  const char *state_class;  // optional
  const char *unit;         // optional
  const char *entity_category; // optional
  const char *state_topic;
  const char *availability_topic; // primary (usually device-level)
  const char *sensor_availability_topic; // optional (sensor-specific)
  const char *payload_on;  // optional (for binary_sensors)
  const char *payload_off; // optional (for binary_sensors)
} ha_discovery_entity_t;

/**
 * @brief Builds the Home Assistant discovery topic.
 *
 * Pattern: homeassistant/<component>/<slug>/<object_id>/config
 */
void ha_discovery_build_topic(char *buf, size_t buf_len, const char *component, const char *slug,
                              const char *object_id);

/**
 * @brief Builds the Home Assistant discovery JSON payload.
 *
 * @return int Number of characters written, or -1 on error/overflow.
 */
int ha_discovery_build_payload(char *buf, size_t buf_len, const ha_discovery_entity_t *entity,
                               const char *slug, const char *friendly_name);

#ifdef __cplusplus
}
#endif

#endif // HA_DISCOVERY_H
