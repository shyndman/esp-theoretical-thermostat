#include "connectivity/runtime_health.h"

#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "sdkconfig.h"

#include "bsp/display.h"
#include "connectivity/mqtt_dataplane.h"
#include "sensors/env_sensors.h"
#include "streaming/whep_endpoint.h"
#include "streaming/webrtc_stream.h"

typedef struct {
  size_t warn_enter;
  size_t warn_clear;
  size_t crit_enter;
  size_t crit_clear;
  uint8_t warn_samples;
  uint8_t crit_samples;
  uint8_t clear_samples;
} runtime_health_stack_thresholds_t;

typedef struct {
  size_t free_warn;
  size_t free_crit;
  size_t free_clear;
  size_t largest_warn;
  size_t largest_crit;
  size_t largest_clear;
  uint16_t ratio_warn_permille;
  uint16_t ratio_crit_permille;
  uint16_t ratio_clear_permille;
  uint8_t warn_samples;
  uint8_t crit_samples;
  uint8_t clear_samples;
} runtime_health_heap_thresholds_t;

typedef struct {
  uint32_t webrtc_alloc_calls;
  uint32_t webrtc_free_calls;
  size_t webrtc_alloc_bytes;
  uint32_t whep_alloc_calls;
  uint32_t whep_free_calls;
  size_t whep_alloc_bytes;
} runtime_health_alloc_churn_totals_t;

typedef struct {
  runtime_health_probe_task_getter_t task_getter;
  TaskHandle_t last_task_handle;
  runtime_health_level_t level;
  runtime_health_level_t pending_level;
  uint8_t pending_count;
} runtime_health_probe_state_t;

typedef struct {
  runtime_health_snapshot_t snapshot;
  runtime_health_probe_state_t probe_state[RUNTIME_HEALTH_PROBE_COUNT];
  runtime_health_level_t heap_level;
  runtime_health_level_t heap_pending_level;
  uint8_t heap_pending_count;
  bool radar_reported;
  size_t radar_last_hwm;
  int64_t next_periodic_log_at_us;
  runtime_health_alloc_churn_totals_t previous_churn_totals;
} runtime_health_state_t;

static const char *TAG = "runtime_health";
static const size_t RADAR_START_STACK_BYTES = 6144;
static const int64_t RUNTIME_HEALTH_LOG_INTERVAL_US = 30 * 1000 * 1000;

static const char *s_probe_names[RUNTIME_HEALTH_PROBE_COUNT] = {
  [RUNTIME_HEALTH_PROBE_MQTT_DATAPLANE] = "mqtt_dataplane",
  [RUNTIME_HEALTH_PROBE_ENV_SENSORS] = "env_sensors",
  [RUNTIME_HEALTH_PROBE_WEBRTC_WORKER] = "webrtc_worker",
  [RUNTIME_HEALTH_PROBE_RADAR_START] = "radar_start",
};

static const runtime_health_stack_thresholds_t s_stack_thresholds = {
  .warn_enter = 1024,
  .warn_clear = 1536,
  .crit_enter = 512,
  .crit_clear = 1024,
  .warn_samples = 2,
  .crit_samples = 2,
  .clear_samples = 2,
};

static const runtime_health_heap_thresholds_t s_heap_thresholds = {
  .free_warn = 96 * 1024,
  .free_crit = 64 * 1024,
  .free_clear = 128 * 1024,
  .largest_warn = 48 * 1024,
  .largest_crit = 32 * 1024,
  .largest_clear = 64 * 1024,
  .ratio_warn_permille = 350,
  .ratio_crit_permille = 250,
  .ratio_clear_permille = 450,
  .warn_samples = 2,
  .crit_samples = 2,
  .clear_samples = 2,
};

static runtime_health_state_t s_state;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

static runtime_health_level_t stack_target_level(runtime_health_level_t current, size_t headroom);
static runtime_health_level_t heap_raw_level(size_t free_bytes,
                                             size_t largest_free_block,
                                             uint16_t ratio_permille);
static runtime_health_level_t heap_target_level(runtime_health_level_t current,
                                                runtime_health_level_t raw_level,
                                                size_t free_bytes,
                                                size_t largest_free_block,
                                                uint16_t ratio_permille);
static bool apply_transition_gate(runtime_health_level_t *current,
                                  runtime_health_level_t target,
                                  runtime_health_level_t *pending,
                                  uint8_t *pending_count,
                                  uint8_t required_samples);
static void sample_stack_probe(runtime_health_probe_id_t probe_id,
                               runtime_health_stack_probe_snapshot_t *probe_snapshot,
                               runtime_health_probe_state_t *probe_state);
static const char *runtime_health_level_name(runtime_health_level_t level);
static esp_log_level_t runtime_health_transition_log_level(runtime_health_level_t level);
static void emit_stack_transition_log(const runtime_health_stack_probe_snapshot_t *probe,
                                      runtime_health_level_t from_level,
                                      runtime_health_level_t to_level);
static void emit_heap_transition_log(const runtime_health_heap_snapshot_t *heap,
                                     runtime_health_level_t from_level,
                                     runtime_health_level_t to_level);
static void emit_periodic_log(const runtime_health_snapshot_t *snapshot);
static size_t display_lvgl_budget_bytes(void);
static size_t configured_stack_budget_bytes(const runtime_health_snapshot_t *snapshot);
static runtime_health_alloc_churn_totals_t collect_alloc_churn_totals(void);
static uint32_t counter_delta_u32(uint32_t current, uint32_t previous);
static size_t counter_delta_size(size_t current, size_t previous);

static void reset_snapshot(void)
{
  memset(&s_state, 0, sizeof(s_state));

  for (size_t i = 0; i < RUNTIME_HEALTH_PROBE_COUNT; ++i) {
    runtime_health_stack_probe_snapshot_t *probe = &s_state.snapshot.stack[i];
    probe->id = (runtime_health_probe_id_t)i;
    probe->name = s_probe_names[i];
    probe->configured = false;
    probe->has_sample = false;
    probe->level = RUNTIME_HEALTH_LEVEL_OK;

    s_state.probe_state[i].level = RUNTIME_HEALTH_LEVEL_OK;
    s_state.probe_state[i].pending_level = RUNTIME_HEALTH_LEVEL_OK;
  }

  s_state.heap_level = RUNTIME_HEALTH_LEVEL_OK;
  s_state.heap_pending_level = RUNTIME_HEALTH_LEVEL_OK;
  s_state.next_periodic_log_at_us = 0;
  s_state.snapshot.heap_internal.level = RUNTIME_HEALTH_LEVEL_OK;
  s_state.snapshot.initialized = true;
}

esp_err_t runtime_health_init(void)
{
  taskENTER_CRITICAL(&s_lock);
  reset_snapshot();
  taskEXIT_CRITICAL(&s_lock);

  const size_t mqtt_stack_bytes = mqtt_dataplane_get_task_stack_size_bytes();
  const size_t env_stack_bytes = env_sensors_get_task_stack_size_bytes();
  const size_t webrtc_stack_bytes = webrtc_stream_get_worker_task_stack_size_bytes();

#if CONFIG_THEO_RAM_WAVE3_STACK_RIGHTSIZE
  ESP_LOGW(TAG,
           "EXPERIMENT ACTIVE: CONFIG_THEO_RAM_WAVE3_STACK_RIGHTSIZE mqtt_stack_b=%zu env_stack_b=%zu webrtc_stack_b=%zu",
           mqtt_stack_bytes,
           env_stack_bytes,
           webrtc_stack_bytes);
#endif

  if (mqtt_stack_bytes > 0) {
    runtime_health_configure_probe(RUNTIME_HEALTH_PROBE_MQTT_DATAPLANE,
                                   mqtt_stack_bytes,
                                   mqtt_dataplane_get_task_handle);
  }

  if (env_stack_bytes > 0) {
    runtime_health_configure_probe(RUNTIME_HEALTH_PROBE_ENV_SENSORS,
                                   env_stack_bytes,
                                   env_sensors_get_task_handle);
  }

  if (webrtc_stack_bytes > 0) {
    runtime_health_configure_probe(RUNTIME_HEALTH_PROBE_WEBRTC_WORKER,
                                   webrtc_stack_bytes,
                                   webrtc_stream_get_worker_task_handle);
  }

  runtime_health_configure_probe(RUNTIME_HEALTH_PROBE_RADAR_START,
                                 RADAR_START_STACK_BYTES,
                                 NULL);

  ESP_LOGI(TAG, "runtime health initialized");
  return ESP_OK;
}

esp_err_t runtime_health_configure_probe(runtime_health_probe_id_t id,
                                         size_t stack_size_bytes,
                                         runtime_health_probe_task_getter_t task_getter)
{
  if (id >= RUNTIME_HEALTH_PROBE_COUNT) {
    return ESP_ERR_INVALID_ARG;
  }
  if (stack_size_bytes == 0) {
    return ESP_ERR_INVALID_ARG;
  }

  taskENTER_CRITICAL(&s_lock);
  if (!s_state.snapshot.initialized) {
    reset_snapshot();
  }

  runtime_health_stack_probe_snapshot_t *probe = &s_state.snapshot.stack[id];
  runtime_health_probe_state_t *probe_state = &s_state.probe_state[id];
  probe->stack_size_bytes = stack_size_bytes;
  probe->configured = true;
  probe_state->task_getter = task_getter;
  probe_state->last_task_handle = NULL;
  taskEXIT_CRITICAL(&s_lock);

  return ESP_OK;
}

void runtime_health_record_radar_start_hwm(size_t headroom_bytes)
{
  taskENTER_CRITICAL(&s_lock);
  if (!s_state.snapshot.initialized) {
    reset_snapshot();
  }
  s_state.radar_last_hwm = headroom_bytes;
  s_state.radar_reported = true;
  taskEXIT_CRITICAL(&s_lock);
}

void runtime_health_periodic_tick(int64_t now_us)
{
  if (now_us <= 0) {
    now_us = esp_timer_get_time();
  }

  taskENTER_CRITICAL(&s_lock);
  if (!s_state.snapshot.initialized) {
    reset_snapshot();
  }
  s_state.snapshot.sampled_at_us = now_us;
  taskEXIT_CRITICAL(&s_lock);

  for (size_t i = 0; i < RUNTIME_HEALTH_PROBE_COUNT; ++i) {
    bool radar_transitioned = false;
    runtime_health_level_t radar_previous_level = RUNTIME_HEALTH_LEVEL_OK;
    runtime_health_level_t radar_current_level = RUNTIME_HEALTH_LEVEL_OK;

    taskENTER_CRITICAL(&s_lock);
    runtime_health_stack_probe_snapshot_t *probe_snapshot = &s_state.snapshot.stack[i];
    runtime_health_probe_state_t *probe_state = &s_state.probe_state[i];

    if ((runtime_health_probe_id_t)i == RUNTIME_HEALTH_PROBE_RADAR_START) {
      if (s_state.radar_reported) {
        const size_t headroom = s_state.radar_last_hwm;
        radar_previous_level = probe_state->level;
        runtime_health_level_t target = stack_target_level(probe_state->level, headroom);
        uint8_t required = (target == RUNTIME_HEALTH_LEVEL_CRIT) ? s_stack_thresholds.crit_samples
                          : (target == RUNTIME_HEALTH_LEVEL_WARN) ? s_stack_thresholds.warn_samples
                                                                   : s_stack_thresholds.clear_samples;
        radar_transitioned = apply_transition_gate(&probe_state->level,
                                                   target,
                                                   &probe_state->pending_level,
                                                   &probe_state->pending_count,
                                                   required);
        probe_snapshot->headroom_bytes = headroom;
        probe_snapshot->used_bytes = (probe_snapshot->stack_size_bytes > headroom)
                                         ? (probe_snapshot->stack_size_bytes - headroom)
                                         : 0;
        probe_snapshot->has_sample = true;
        probe_snapshot->level = probe_state->level;
        radar_current_level = probe_state->level;
      }
      taskEXIT_CRITICAL(&s_lock);
      if (radar_transitioned) {
        emit_stack_transition_log(probe_snapshot, radar_previous_level, radar_current_level);
      }
      continue;
    }

    taskEXIT_CRITICAL(&s_lock);
    sample_stack_probe((runtime_health_probe_id_t)i, probe_snapshot, probe_state);
  }

  const size_t free_bytes = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  const size_t min_free = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
  const size_t largest_free = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
  const uint16_t ratio_permille =
      (free_bytes == 0) ? 0 : (uint16_t)((largest_free * 1000U) / free_bytes);

  taskENTER_CRITICAL(&s_lock);
  runtime_health_heap_snapshot_t *heap = &s_state.snapshot.heap_internal;
  runtime_health_level_t raw_level = heap_raw_level(free_bytes, largest_free, ratio_permille);
  runtime_health_level_t target = heap_target_level(s_state.heap_level,
                                                    raw_level,
                                                    free_bytes,
                                                    largest_free,
                                                    ratio_permille);

  uint8_t required = (target == RUNTIME_HEALTH_LEVEL_CRIT) ? s_heap_thresholds.crit_samples
                    : (target == RUNTIME_HEALTH_LEVEL_WARN) ? s_heap_thresholds.warn_samples
                                                             : s_heap_thresholds.clear_samples;

  const runtime_health_level_t previous_level = s_state.heap_level;
  bool heap_transitioned = apply_transition_gate(&s_state.heap_level,
                                                 target,
                                                 &s_state.heap_pending_level,
                                                 &s_state.heap_pending_count,
                                                 required);
  runtime_health_heap_snapshot_t heap_copy = {0};
  runtime_health_level_t current_level = s_state.heap_level;

  heap->has_sample = true;
  heap->free_bytes = free_bytes;
  heap->minimum_free_bytes = min_free;
  heap->largest_free_block_bytes = largest_free;
  heap->largest_free_ratio_permille = ratio_permille;
  heap->level = current_level;
  if (heap_transitioned) {
    heap_copy = *heap;
  }

  const bool emit_periodic = (s_state.next_periodic_log_at_us == 0) ||
                             (now_us >= s_state.next_periodic_log_at_us);
  if (emit_periodic) {
    s_state.next_periodic_log_at_us = now_us + RUNTIME_HEALTH_LOG_INTERVAL_US;
  }

  runtime_health_snapshot_t snapshot_copy;
  bool have_periodic_snapshot = false;
  if (emit_periodic) {
    memcpy(&snapshot_copy, &s_state.snapshot, sizeof(snapshot_copy));
    have_periodic_snapshot = true;
  }
  taskEXIT_CRITICAL(&s_lock);

  if (heap_transitioned) {
    emit_heap_transition_log(&heap_copy, previous_level, current_level);
  }

  if (have_periodic_snapshot) {
    emit_periodic_log(&snapshot_copy);
  }
}

esp_err_t runtime_health_get_snapshot(runtime_health_snapshot_t *snapshot)
{
  if (!snapshot) {
    return ESP_ERR_INVALID_ARG;
  }

  taskENTER_CRITICAL(&s_lock);
  if (!s_state.snapshot.initialized) {
    reset_snapshot();
  }
  memcpy(snapshot, &s_state.snapshot, sizeof(*snapshot));
  taskEXIT_CRITICAL(&s_lock);
  return ESP_OK;
}

static void sample_stack_probe(runtime_health_probe_id_t probe_id,
                               runtime_health_stack_probe_snapshot_t *probe_snapshot,
                               runtime_health_probe_state_t *probe_state)
{
  if (!probe_snapshot->configured || !probe_state->task_getter) {
    return;
  }

  if (probe_state->last_task_handle == NULL) {
    probe_state->last_task_handle = probe_state->task_getter();
  }

  if (probe_state->last_task_handle == NULL) {
    return;
  }

  const size_t headroom = (size_t)uxTaskGetStackHighWaterMark2(probe_state->last_task_handle);
  runtime_health_level_t target = stack_target_level(probe_state->level, headroom);

  uint8_t required = (target == RUNTIME_HEALTH_LEVEL_CRIT) ? s_stack_thresholds.crit_samples
                    : (target == RUNTIME_HEALTH_LEVEL_WARN) ? s_stack_thresholds.warn_samples
                                                             : s_stack_thresholds.clear_samples;

  runtime_health_level_t previous_level = RUNTIME_HEALTH_LEVEL_OK;
  bool transitioned = false;

  taskENTER_CRITICAL(&s_lock);
  previous_level = probe_state->level;
  transitioned = apply_transition_gate(&probe_state->level,
                                       target,
                                       &probe_state->pending_level,
                                       &probe_state->pending_count,
                                       required);
  probe_snapshot->headroom_bytes = headroom;
  probe_snapshot->used_bytes = (probe_snapshot->stack_size_bytes > headroom)
                                   ? (probe_snapshot->stack_size_bytes - headroom)
                                   : 0;
  probe_snapshot->has_sample = true;
  probe_snapshot->level = probe_state->level;
  taskEXIT_CRITICAL(&s_lock);

  if (transitioned) {
    (void)probe_id;
    emit_stack_transition_log(probe_snapshot, previous_level, probe_state->level);
  }
}

static runtime_health_level_t stack_target_level(runtime_health_level_t current, size_t headroom)
{
  switch (current) {
    case RUNTIME_HEALTH_LEVEL_CRIT:
      if (headroom < s_stack_thresholds.crit_clear) {
        return RUNTIME_HEALTH_LEVEL_CRIT;
      }
      if (headroom <= s_stack_thresholds.warn_enter) {
        return RUNTIME_HEALTH_LEVEL_WARN;
      }
      return RUNTIME_HEALTH_LEVEL_OK;
    case RUNTIME_HEALTH_LEVEL_WARN:
      if (headroom <= s_stack_thresholds.crit_enter) {
        return RUNTIME_HEALTH_LEVEL_CRIT;
      }
      if (headroom >= s_stack_thresholds.warn_clear) {
        return RUNTIME_HEALTH_LEVEL_OK;
      }
      return RUNTIME_HEALTH_LEVEL_WARN;
    case RUNTIME_HEALTH_LEVEL_OK:
    default:
      if (headroom <= s_stack_thresholds.crit_enter) {
        return RUNTIME_HEALTH_LEVEL_CRIT;
      }
      if (headroom <= s_stack_thresholds.warn_enter) {
        return RUNTIME_HEALTH_LEVEL_WARN;
      }
      return RUNTIME_HEALTH_LEVEL_OK;
  }
}

static runtime_health_level_t heap_raw_level(size_t free_bytes,
                                             size_t largest_free_block,
                                             uint16_t ratio_permille)
{
  if (free_bytes <= s_heap_thresholds.free_crit ||
      largest_free_block <= s_heap_thresholds.largest_crit ||
      ratio_permille <= s_heap_thresholds.ratio_crit_permille) {
    return RUNTIME_HEALTH_LEVEL_CRIT;
  }

  if (free_bytes <= s_heap_thresholds.free_warn ||
      largest_free_block <= s_heap_thresholds.largest_warn ||
      ratio_permille <= s_heap_thresholds.ratio_warn_permille) {
    return RUNTIME_HEALTH_LEVEL_WARN;
  }

  return RUNTIME_HEALTH_LEVEL_OK;
}

static runtime_health_level_t heap_target_level(runtime_health_level_t current,
                                                runtime_health_level_t raw_level,
                                                size_t free_bytes,
                                                size_t largest_free_block,
                                                uint16_t ratio_permille)
{
  if (raw_level == RUNTIME_HEALTH_LEVEL_CRIT) {
    return RUNTIME_HEALTH_LEVEL_CRIT;
  }

  if (current == RUNTIME_HEALTH_LEVEL_CRIT) {
    if (free_bytes < s_heap_thresholds.free_clear ||
        largest_free_block < s_heap_thresholds.largest_clear ||
        ratio_permille < s_heap_thresholds.ratio_clear_permille) {
      return RUNTIME_HEALTH_LEVEL_CRIT;
    }
    return (raw_level == RUNTIME_HEALTH_LEVEL_WARN)
               ? RUNTIME_HEALTH_LEVEL_WARN
               : RUNTIME_HEALTH_LEVEL_OK;
  }

  if (current == RUNTIME_HEALTH_LEVEL_WARN) {
    if (raw_level == RUNTIME_HEALTH_LEVEL_WARN) {
      return RUNTIME_HEALTH_LEVEL_WARN;
    }
    if (free_bytes < s_heap_thresholds.free_clear ||
        largest_free_block < s_heap_thresholds.largest_clear ||
        ratio_permille < s_heap_thresholds.ratio_clear_permille) {
      return RUNTIME_HEALTH_LEVEL_WARN;
    }
    return RUNTIME_HEALTH_LEVEL_OK;
  }

  return raw_level;
}

static bool apply_transition_gate(runtime_health_level_t *current,
                                  runtime_health_level_t target,
                                  runtime_health_level_t *pending,
                                  uint8_t *pending_count,
                                  uint8_t required_samples)
{
  if (*current == target) {
    *pending = target;
    *pending_count = 0;
    return false;
  }

  if (*pending != target) {
    *pending = target;
    *pending_count = 1;
  } else if (*pending_count < UINT8_MAX) {
    *pending_count += 1;
  }

  if (*pending_count >= required_samples) {
    *current = target;
    *pending_count = 0;
    return true;
  }

  return false;
}

static const char *runtime_health_level_name(runtime_health_level_t level)
{
  switch (level) {
    case RUNTIME_HEALTH_LEVEL_WARN:
      return "WARN";
    case RUNTIME_HEALTH_LEVEL_CRIT:
      return "CRIT";
    case RUNTIME_HEALTH_LEVEL_OK:
    default:
      return "OK";
  }
}

static esp_log_level_t runtime_health_transition_log_level(runtime_health_level_t level)
{
  if (level == RUNTIME_HEALTH_LEVEL_CRIT) {
    return ESP_LOG_ERROR;
  }
  if (level == RUNTIME_HEALTH_LEVEL_WARN) {
    return ESP_LOG_WARN;
  }
  return ESP_LOG_INFO;
}

static void emit_stack_transition_log(const runtime_health_stack_probe_snapshot_t *probe,
                                      runtime_health_level_t from_level,
                                      runtime_health_level_t to_level)
{
  if (!probe) {
    return;
  }

  ESP_LOG_LEVEL(runtime_health_transition_log_level(to_level),
                TAG,
                "runtime_health_transition domain=stack probe=%s from=%s to=%s headroom_b=%zu used_b=%zu stack_b=%zu",
                probe->name ? probe->name : "unknown",
                runtime_health_level_name(from_level),
                runtime_health_level_name(to_level),
                probe->headroom_bytes,
                probe->used_bytes,
                probe->stack_size_bytes);
}

static void emit_heap_transition_log(const runtime_health_heap_snapshot_t *heap,
                                     runtime_health_level_t from_level,
                                     runtime_health_level_t to_level)
{
  if (!heap) {
    return;
  }

  ESP_LOG_LEVEL(runtime_health_transition_log_level(to_level),
                TAG,
                "runtime_health_transition domain=heap scope=internal from=%s to=%s free_b=%zu min_b=%zu largest_b=%zu largest_ratio_permille=%u",
                runtime_health_level_name(from_level),
                runtime_health_level_name(to_level),
                heap->free_bytes,
                heap->minimum_free_bytes,
                heap->largest_free_block_bytes,
                heap->largest_free_ratio_permille);
}

static void emit_periodic_log(const runtime_health_snapshot_t *snapshot)
{
  if (!snapshot) {
    return;
  }

  const runtime_health_stack_probe_snapshot_t *mqtt =
      &snapshot->stack[RUNTIME_HEALTH_PROBE_MQTT_DATAPLANE];
  const runtime_health_stack_probe_snapshot_t *env =
      &snapshot->stack[RUNTIME_HEALTH_PROBE_ENV_SENSORS];
  const runtime_health_stack_probe_snapshot_t *webrtc =
      &snapshot->stack[RUNTIME_HEALTH_PROBE_WEBRTC_WORKER];
  const runtime_health_stack_probe_snapshot_t *radar =
      &snapshot->stack[RUNTIME_HEALTH_PROBE_RADAR_START];
  const runtime_health_heap_snapshot_t *heap = &snapshot->heap_internal;

  ESP_LOGI(TAG,
           "runtime_health_obs ts_us=%lld "
           "stack_mqtt_headroom_b=%zu stack_mqtt_level=%s "
           "stack_env_headroom_b=%zu stack_env_level=%s "
           "stack_webrtc_headroom_b=%zu stack_webrtc_level=%s "
           "stack_radar_headroom_b=%zu stack_radar_level=%s "
           "heap_internal_free_b=%zu heap_internal_min_b=%zu "
           "heap_internal_largest_b=%zu heap_internal_ratio_permille=%u "
           "heap_internal_risk=%s",
           (long long)snapshot->sampled_at_us,
           mqtt->headroom_bytes,
           runtime_health_level_name(mqtt->level),
           env->headroom_bytes,
           runtime_health_level_name(env->level),
           webrtc->headroom_bytes,
           runtime_health_level_name(webrtc->level),
           radar->headroom_bytes,
           runtime_health_level_name(radar->level),
           heap->free_bytes,
           heap->minimum_free_bytes,
           heap->largest_free_block_bytes,
           heap->largest_free_ratio_permille,
           runtime_health_level_name(heap->level));

  webrtc_stream_ram_budget_t webrtc_budget = {0};
  webrtc_stream_get_ram_budget(&webrtc_budget);

  const size_t display_lvgl_b = display_lvgl_budget_bytes();
  const size_t webrtc_pool_cache_b = webrtc_budget.total_pool_cache_bytes;
  const size_t mqtt_queue_est_b =
      mqtt_dataplane_get_queue_depth() * mqtt_dataplane_get_queue_item_size_bytes();
  const size_t stack_budget_b = configured_stack_budget_bytes(snapshot);

  const size_t known_budget_b = display_lvgl_b + webrtc_pool_cache_b + mqtt_queue_est_b + stack_budget_b;

  const uint16_t display_ratio_permille =
      (known_budget_b == 0) ? 0 : (uint16_t)((display_lvgl_b * 1000U) / known_budget_b);
  const uint16_t webrtc_ratio_permille =
      (known_budget_b == 0) ? 0 : (uint16_t)((webrtc_pool_cache_b * 1000U) / known_budget_b);
  const uint16_t mqtt_ratio_permille =
      (known_budget_b == 0) ? 0 : (uint16_t)((mqtt_queue_est_b * 1000U) / known_budget_b);
  const uint16_t stack_ratio_permille =
      (known_budget_b == 0) ? 0 : (uint16_t)((stack_budget_b * 1000U) / known_budget_b);

  const runtime_health_alloc_churn_totals_t churn_totals = collect_alloc_churn_totals();

  taskENTER_CRITICAL(&s_lock);
  const runtime_health_alloc_churn_totals_t previous = s_state.previous_churn_totals;
  s_state.previous_churn_totals = churn_totals;
  taskEXIT_CRITICAL(&s_lock);

  const uint32_t webrtc_alloc_delta =
      counter_delta_u32(churn_totals.webrtc_alloc_calls, previous.webrtc_alloc_calls);
  const uint32_t webrtc_free_delta =
      counter_delta_u32(churn_totals.webrtc_free_calls, previous.webrtc_free_calls);
  const size_t webrtc_alloc_b_delta =
      counter_delta_size(churn_totals.webrtc_alloc_bytes, previous.webrtc_alloc_bytes);
  const uint32_t whep_alloc_delta =
      counter_delta_u32(churn_totals.whep_alloc_calls, previous.whep_alloc_calls);
  const uint32_t whep_free_delta =
      counter_delta_u32(churn_totals.whep_free_calls, previous.whep_free_calls);
  const size_t whep_alloc_b_delta =
      counter_delta_size(churn_totals.whep_alloc_bytes, previous.whep_alloc_bytes);

  ESP_LOGI(TAG,
           "runtime_health_ram_attr ts_us=%lld "
           "display_lvgl_b=%zu display_lvgl_ratio_permille=%u "
           "webrtc_pool_cache_b=%zu webrtc_pool_cache_ratio_permille=%u webrtc_wave1=%u mqtt_wave2=%u stack_wave3=%u "
           "mqtt_queue_est_b=%zu mqtt_queue_ratio_permille=%u "
           "stack_budget_b=%zu stack_budget_ratio_permille=%u "
           "dyn_webrtc_alloc_delta=%u dyn_webrtc_free_delta=%u dyn_webrtc_alloc_b_delta=%zu "
           "dyn_whep_alloc_delta=%u dyn_whep_free_delta=%u dyn_whep_alloc_b_delta=%zu "
           "dyn_alloc_ops_delta=%u dyn_alloc_b_delta=%zu "
           "known_budget_b=%zu heap_internal_free_b=%zu",
           (long long)snapshot->sampled_at_us,
           display_lvgl_b,
           display_ratio_permille,
           webrtc_pool_cache_b,
           webrtc_ratio_permille,
           webrtc_budget.wave1_tuning_enabled ? 1U : 0U,
           mqtt_dataplane_wave2_queue_tuning_enabled() ? 1U : 0U,
#if CONFIG_THEO_RAM_WAVE3_STACK_RIGHTSIZE
           1U,
#else
           0U,
#endif
           mqtt_queue_est_b,
           mqtt_ratio_permille,
           stack_budget_b,
           stack_ratio_permille,
           webrtc_alloc_delta,
           webrtc_free_delta,
           webrtc_alloc_b_delta,
           whep_alloc_delta,
           whep_free_delta,
           whep_alloc_b_delta,
           webrtc_alloc_delta + webrtc_free_delta + whep_alloc_delta + whep_free_delta,
           webrtc_alloc_b_delta + whep_alloc_b_delta,
           known_budget_b,
           heap->free_bytes);
}

static size_t display_lvgl_budget_bytes(void)
{
  size_t lvgl_heap_b = 0;
#if defined(CONFIG_LV_MEM_SIZE)
  lvgl_heap_b = (size_t)CONFIG_LV_MEM_SIZE;
#elif defined(CONFIG_LV_MEM_SIZE_KILOBYTES)
  lvgl_heap_b = (size_t)CONFIG_LV_MEM_SIZE_KILOBYTES * 1024U;
#endif
  const size_t display_frame_b = (size_t)BSP_LCD_H_RES * (size_t)BSP_LCD_V_RES *
                                 ((size_t)BSP_LCD_BITS_PER_PIXEL / 8U);
  const size_t display_buffers_b = display_frame_b * (size_t)CONFIG_BSP_LCD_DPI_BUFFER_NUMS;
  return lvgl_heap_b + display_buffers_b;
}

static size_t configured_stack_budget_bytes(const runtime_health_snapshot_t *snapshot)
{
  if (!snapshot) {
    return 0;
  }

  size_t total = 0;
  for (size_t i = 0; i < RUNTIME_HEALTH_PROBE_COUNT; ++i) {
    const runtime_health_stack_probe_snapshot_t *probe = &snapshot->stack[i];
    if (!probe->configured) {
      continue;
    }
    total += probe->stack_size_bytes;
  }

  return total;
}

static runtime_health_alloc_churn_totals_t collect_alloc_churn_totals(void)
{
  webrtc_stream_alloc_churn_t webrtc = {0};
  whep_endpoint_alloc_churn_t whep = {0};
  webrtc_stream_get_alloc_churn_snapshot(&webrtc);
  whep_endpoint_get_alloc_churn_snapshot(&whep);

  runtime_health_alloc_churn_totals_t totals = {
    .webrtc_alloc_calls = webrtc.alloc_calls,
    .webrtc_free_calls = webrtc.free_calls,
    .webrtc_alloc_bytes = webrtc.alloc_bytes,
    .whep_alloc_calls = whep.alloc_calls,
    .whep_free_calls = whep.free_calls,
    .whep_alloc_bytes = whep.alloc_bytes,
  };

  return totals;
}

static uint32_t counter_delta_u32(uint32_t current, uint32_t previous)
{
  return (current >= previous) ? (current - previous) : current;
}

static size_t counter_delta_size(size_t current, size_t previous)
{
  return (current >= previous) ? (current - previous) : current;
}
