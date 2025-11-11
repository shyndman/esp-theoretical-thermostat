#pragma once

#include "esp_err.h"

/**
 * @brief Attempt to play the boot chime, respecting mute/quiet-hour settings.
 *
 * @return ESP_OK on success or when playback is intentionally skipped, error otherwise.
 */
esp_err_t thermostat_audio_boot_try_play(void);

