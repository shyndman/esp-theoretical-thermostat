#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Cached environmental sensor readings.
 * All values represent the most recent successful read; check the _valid
 * flags to determine if readings are available.
 */
typedef struct {
  float temperature_aht_c;
  float temperature_bmp_c;
  float relative_humidity;
  float air_pressure_kpa;
  bool temperature_aht_valid;
  bool temperature_bmp_valid;
  bool relative_humidity_valid;
  bool air_pressure_valid;
} env_sensor_readings_t;

/**
 * Initialize the environmental sensor subsystem and start the sampling task.
 *
 * This function:
 * 1. Creates a shared I2C master bus on the configured GPIO pins
 * 2. Initializes the AHT20 (temperature/humidity) and BMP280 (temperature/pressure) sensors
 * 3. Spawns a FreeRTOS task that samples at CONFIG_THEO_SENSOR_POLL_SECONDS interval
 * 4. Publishes telemetry to MQTT when connected
 *
 * @return ESP_OK on success, or an error code if initialization fails.
 *         Fatal sensor failures (missing ACK, wrong chip ID) return errors
 *         that should halt boot.
 */
esp_err_t env_sensors_start(void);

/**
 * Get the latest cached sensor readings.
 *
 * Thread-safe; may be called from any context.
 *
 * @param[out] readings  Pointer to structure to populate with current values
 * @return ESP_OK if at least one valid reading exists, ESP_ERR_NOT_FOUND if
 *         no readings have been captured yet
 */
esp_err_t env_sensors_get_readings(env_sensor_readings_t *readings);

/**
 * Check whether all environmental sensors are currently online.
 *
 * @return true if all sensors have reported valid readings within the
 *         failure threshold, false otherwise
 */
bool env_sensors_all_online(void);

/**
 * Get the Theo-owned MQTT base topic.
 *
 * Returns the normalized base topic derived from CONFIG_THEO_THEOSTAT_BASE_TOPIC
 * or auto-generated from the device slug. Only valid after env_sensors_start().
 *
 * @return Pointer to the base topic string (static storage)
 */
const char *env_sensors_get_theo_base_topic(void);

/**
 * Get the normalized device slug.
 *
 * Returns the sanitized slug derived from CONFIG_THEO_DEVICE_SLUG.
 * Only valid after env_sensors_start().
 *
 * @return Pointer to the device slug string (static storage)
 */
const char *env_sensors_get_device_slug(void);

#ifdef __cplusplus
}
#endif
