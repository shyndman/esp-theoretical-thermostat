#pragma once

#include "esp_err.h"

esp_err_t thermostat_audio_boot_prepare(void);
esp_err_t thermostat_audio_boot_try_play(void);
esp_err_t thermostat_audio_boot_play_failure(void);
