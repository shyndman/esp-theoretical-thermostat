#pragma once

#include <stddef.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t camera_snapshot_publisher_start(void);
esp_err_t camera_snapshot_publisher_stop(void);
TaskHandle_t camera_snapshot_publisher_get_task_handle(void);
size_t camera_snapshot_publisher_get_task_stack_size_bytes(void);

#ifdef __cplusplus
}
#endif
