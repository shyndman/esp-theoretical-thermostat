#pragma once

#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "esp_err.h"

esp_err_t time_sync_start(void);
bool time_sync_wait_for_sync(TickType_t timeout_ticks);
