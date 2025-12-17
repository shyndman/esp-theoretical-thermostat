/**
 * @file radar_presence.h
 * @brief LD2410C mmWave radar presence detection
 *
 * Provides presence detection state and MQTT telemetry for backlight wake
 * and Home Assistant integration.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

/**
 * @brief Radar presence state snapshot
 */
typedef struct {
  bool presence_detected;       ///< Any target (moving or still) detected
  uint16_t detection_distance_cm;   ///< Combined detection distance
  uint16_t moving_distance_cm;      ///< Moving target distance (0 if none)
  uint16_t still_distance_cm;       ///< Still target distance (0 if none)
  uint8_t moving_energy;            ///< Moving target energy (0-100)
  uint8_t still_energy;             ///< Still target energy (0-100)
  int64_t last_update_us;           ///< esp_timer_get_time() of last valid frame
} radar_presence_state_t;

/**
 * @brief Initialize and start the radar presence sensor
 *
 * Initializes the LD2410C UART, spawns the polling task, and begins
 * publishing MQTT telemetry when connected.
 *
 * @return ESP_OK on success, error code on failure
 */
esp_err_t radar_presence_start(void);

/**
 * @brief Stop the radar presence sensor
 *
 * Stops the polling task and releases resources.
 *
 * @return ESP_OK on success
 */
esp_err_t radar_presence_stop(void);

/**
 * @brief Get the current radar presence state
 *
 * Thread-safe accessor for the cached radar state. The state is updated
 * by the polling task at approximately 10 Hz.
 *
 * @param[out] out Pointer to state struct to fill
 * @return true if state is valid (radar online), false if offline/invalid
 */
bool radar_presence_get_state(radar_presence_state_t *out);

/**
 * @brief Check if the radar sensor is online
 *
 * @return true if radar is responding with valid frames, false if offline
 */
bool radar_presence_is_online(void);
