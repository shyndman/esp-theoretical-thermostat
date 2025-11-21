#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

/**
 * Shared gate for application cues (audio, LEDs, etc.).
 * Returns ESP_OK when the cue may play, ESP_ERR_DISABLED when the build flag
 * disables the feature, and ESP_ERR_INVALID_STATE when quiet hours or clock
 * readiness block output.
 */
esp_err_t thermostat_application_cues_check(const char *cue_name, bool feature_enabled);

/**
 * Format helper for documentation/logs.
 * Returns true when quiet hours are configured and writes "HH:MM-HH:MM" into
 * the provided buffer (including null terminator) when possible.
 */
bool thermostat_application_cues_window_string(char *buffer, size_t buffer_len);
