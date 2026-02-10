#pragma once

#include <stddef.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t webrtc_stream_start(void);
void webrtc_stream_stop(void);
TaskHandle_t webrtc_stream_get_worker_task_handle(void);
size_t webrtc_stream_get_worker_task_stack_size_bytes(void);

#ifdef __cplusplus
}
#endif
