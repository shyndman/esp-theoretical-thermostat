#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  bool wave1_tuning_enabled;
  size_t data_send_cache_bytes;
  size_t data_recv_cache_bytes;
  size_t audio_recv_jitter_cache_bytes;
  size_t video_recv_jitter_cache_bytes;
  size_t rtp_send_pool_bytes;
  size_t rtp_send_queue_entries;
  size_t total_pool_cache_bytes;
} webrtc_stream_ram_budget_t;

typedef struct {
  uint32_t alloc_calls;
  uint32_t free_calls;
  size_t alloc_bytes;
} webrtc_stream_alloc_churn_t;

esp_err_t webrtc_stream_start(void);
void webrtc_stream_stop(void);
TaskHandle_t webrtc_stream_get_worker_task_handle(void);
size_t webrtc_stream_get_worker_task_stack_size_bytes(void);
void webrtc_stream_get_ram_budget(webrtc_stream_ram_budget_t *budget);
void webrtc_stream_get_alloc_churn_snapshot(webrtc_stream_alloc_churn_t *snapshot);

#ifdef __cplusplus
}
#endif
