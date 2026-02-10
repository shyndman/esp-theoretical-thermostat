#include "connectivity/runtime_health.h"

#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

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
} runtime_health_state_t;

static const char *TAG = "runtime_health";

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
static void apply_transition_gate(runtime_health_level_t *current,
                                  runtime_health_level_t target,
                                  runtime_health_level_t *pending,
                                  uint8_t *pending_count,
                                  uint8_t required_samples);
static void sample_stack_probe(runtime_health_probe_id_t probe_id,
                               runtime_health_stack_probe_snapshot_t *probe_snapshot,
                               runtime_health_probe_state_t *probe_state);

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
  s_state.snapshot.heap_internal.level = RUNTIME_HEALTH_LEVEL_OK;
  s_state.snapshot.initialized = true;
}

esp_err_t runtime_health_init(void)
{
  taskENTER_CRITICAL(&s_lock);
  reset_snapshot();
  taskEXIT_CRITICAL(&s_lock);

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
    taskENTER_CRITICAL(&s_lock);
    runtime_health_stack_probe_snapshot_t *probe_snapshot = &s_state.snapshot.stack[i];
    runtime_health_probe_state_t *probe_state = &s_state.probe_state[i];

    if ((runtime_health_probe_id_t)i == RUNTIME_HEALTH_PROBE_RADAR_START) {
      if (s_state.radar_reported) {
        const size_t headroom = s_state.radar_last_hwm;
        runtime_health_level_t target = stack_target_level(probe_state->level, headroom);
        uint8_t required = (target == RUNTIME_HEALTH_LEVEL_CRIT) ? s_stack_thresholds.crit_samples
                          : (target == RUNTIME_HEALTH_LEVEL_WARN) ? s_stack_thresholds.warn_samples
                                                                   : s_stack_thresholds.clear_samples;
        apply_transition_gate(&probe_state->level,
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
      }
      taskEXIT_CRITICAL(&s_lock);
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

  apply_transition_gate(&s_state.heap_level,
                        target,
                        &s_state.heap_pending_level,
                        &s_state.heap_pending_count,
                        required);

  heap->has_sample = true;
  heap->free_bytes = free_bytes;
  heap->minimum_free_bytes = min_free;
  heap->largest_free_block_bytes = largest_free;
  heap->largest_free_ratio_permille = ratio_permille;
  heap->level = s_state.heap_level;
  taskEXIT_CRITICAL(&s_lock);
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
  (void)probe_id;
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

  taskENTER_CRITICAL(&s_lock);
  apply_transition_gate(&probe_state->level,
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

static void apply_transition_gate(runtime_health_level_t *current,
                                  runtime_health_level_t target,
                                  runtime_health_level_t *pending,
                                  uint8_t *pending_count,
                                  uint8_t required_samples)
{
  if (*current == target) {
    *pending = target;
    *pending_count = 0;
    return;
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
  }
}
