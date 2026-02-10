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

typedef enum {
  RUNTIME_HEALTH_LEVEL_OK = 0,
  RUNTIME_HEALTH_LEVEL_WARN,
  RUNTIME_HEALTH_LEVEL_CRIT,
} runtime_health_level_t;

typedef enum {
  RUNTIME_HEALTH_PROBE_MQTT_DATAPLANE = 0,
  RUNTIME_HEALTH_PROBE_ENV_SENSORS,
  RUNTIME_HEALTH_PROBE_WEBRTC_WORKER,
  RUNTIME_HEALTH_PROBE_RADAR_START,
  RUNTIME_HEALTH_PROBE_COUNT,
} runtime_health_probe_id_t;

typedef TaskHandle_t (*runtime_health_probe_task_getter_t)(void);

typedef struct {
  runtime_health_probe_id_t id;
  const char *name;
  bool configured;
  bool has_sample;
  size_t stack_size_bytes;
  size_t headroom_bytes;
  size_t used_bytes;
  runtime_health_level_t level;
} runtime_health_stack_probe_snapshot_t;

typedef struct {
  bool has_sample;
  size_t free_bytes;
  size_t minimum_free_bytes;
  size_t largest_free_block_bytes;
  uint16_t largest_free_ratio_permille;
  runtime_health_level_t level;
} runtime_health_heap_snapshot_t;

typedef struct {
  bool initialized;
  int64_t sampled_at_us;
  runtime_health_stack_probe_snapshot_t stack[RUNTIME_HEALTH_PROBE_COUNT];
  runtime_health_heap_snapshot_t heap_internal;
} runtime_health_snapshot_t;

esp_err_t runtime_health_init(void);
void runtime_health_periodic_tick(int64_t now_us);
void runtime_health_record_radar_start_hwm(size_t headroom_bytes);
esp_err_t runtime_health_get_snapshot(runtime_health_snapshot_t *snapshot);
esp_err_t runtime_health_configure_probe(runtime_health_probe_id_t id,
                                         size_t stack_size_bytes,
                                         runtime_health_probe_task_getter_t task_getter);

#ifdef __cplusplus
}
#endif
