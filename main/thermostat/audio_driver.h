#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

esp_err_t thermostat_audio_driver_init(void);
esp_err_t thermostat_audio_driver_set_volume(int percent);
esp_err_t thermostat_audio_driver_play(const uint8_t *pcm, size_t len);
