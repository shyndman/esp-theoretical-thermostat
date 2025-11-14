#include "thermostat/remote_setpoint_controller.h"

#include <math.h>
#include <string.h>
#include "esp_log.h"
#include "lvgl.h"

#include "thermostat/backlight_manager.h"
#include "thermostat/ui_setpoint_view.h"
#include "thermostat/ui_setpoint_input.h"

#define REMOTE_VALUE_EPSILON         (0.05f)
#define REMOTE_WAIT_LIT_POLL_MS      (50)
#define REMOTE_PRE_DELAY_MS          (1000)
#define REMOTE_ANIMATION_MS          (1600)
#define REMOTE_POST_DELAY_MS         (1000)
#define REMOTE_SLEEP_TIMEOUT_MS      (5000)

typedef enum {
  REMOTE_PHASE_IDLE = 0,
  REMOTE_PHASE_WAIT_LIT,
  REMOTE_PHASE_PRE_DELAY,
  REMOTE_PHASE_ANIMATING,
  REMOTE_PHASE_POST_DELAY,
} remote_phase_t;

typedef struct
{
  float cooling_target_c;
  float heating_target_c;
  bool cooling_valid;
  bool heating_valid;
} remote_session_t;

typedef struct
{
  thermostat_target_t target;
  bool valid;
} temp_anim_ctx_t;

typedef struct
{
  bool initialized;
  remote_phase_t phase;
  bool has_pending;
  bool awaiting_initial_delay;
  bool initial_wake_consumed;
  remote_session_t current;
  remote_session_t pending;
  float desired_cooling_c;
  float desired_heating_c;
  bool desired_cooling_valid;
  bool desired_heating_valid;
  lv_timer_t *wait_lit_timer;
  lv_timer_t *pre_delay_timer;
  lv_timer_t *post_delay_timer;
  uint8_t active_anim_count;
  temp_anim_ctx_t cooling_anim_ctx;
  temp_anim_ctx_t heating_anim_ctx;
  uint32_t last_remote_serial;
} remote_controller_t;

static remote_controller_t s_remote;
static const char *TAG = "remote_sp";

static void remote_reset_state(void);
static bool remote_update_target_value(thermostat_target_t target, float value_c);
static void remote_prepare_session(remote_session_t *session);
static void remote_queue_session(const remote_session_t *session);
static void remote_start_wait_for_light(void);
static void remote_wait_lit_cb(lv_timer_t *timer);
static void remote_start_pre_delay(void);
static void remote_pre_delay_cb(lv_timer_t *timer);
static void remote_start_animation(void);
static void remote_start_pending_session(void);
static void remote_anim_exec_apply_temp(void *ctx, int32_t value);
static void remote_start_anim(void *var, lv_anim_exec_xcb_t exec_cb, int32_t start, int32_t end);
static void remote_start_temp_anim(temp_anim_ctx_t *ctx, float start_c, float target_c);
static void remote_animation_ready_cb(lv_anim_t *anim);
static void remote_handle_animation_complete(void);
static void remote_start_post_delay(void);
static void remote_post_delay_cb(lv_timer_t *timer);
static void remote_cancel_timer(lv_timer_t **timer);
static void remote_schedule_sleep_if_needed(void);
static void remote_finish_burst(void);
static void remote_poke_activity(void);
static int32_t remote_temp_to_anim_value(float value_c);

void thermostat_remote_setpoint_controller_init(void)
{
  remote_reset_state();
  s_remote.desired_cooling_c = g_view_model.cooling_setpoint_c;
  s_remote.desired_heating_c = g_view_model.heating_setpoint_c;
  s_remote.desired_cooling_valid = g_view_model.cooling_setpoint_valid;
  s_remote.desired_heating_valid = g_view_model.heating_setpoint_valid;
  s_remote.cooling_anim_ctx.target = THERMOSTAT_TARGET_COOL;
  s_remote.heating_anim_ctx.target = THERMOSTAT_TARGET_HEAT;
  s_remote.initialized = true;
  ESP_LOGI(TAG, "Remote setpoint controller ready");
}

void thermostat_remote_setpoint_controller_submit(thermostat_target_t target, float value_c)
{
  float previous_value = (target == THERMOSTAT_TARGET_COOL) ? g_view_model.cooling_setpoint_c
                                                           : g_view_model.heating_setpoint_c;
  bool changed = remote_update_target_value(target, value_c);
  if (!changed && s_remote.phase == REMOTE_PHASE_IDLE) {
    ESP_LOGD(TAG, "[remote] target=%d value unchanged (%.2f)", target, value_c);
    return;
  }

  remote_session_t next_session = {0};
  remote_prepare_session(&next_session);

  if (!s_remote.initialized) {
    ESP_LOGW(TAG, "[remote] controller not initialized; applying setpoint immediately");
    thermostat_update_setpoint_labels();
    thermostat_update_track_geometry();
    thermostat_position_setpoint_labels();
    return;
  }

  if (!changed && s_remote.phase != REMOTE_PHASE_IDLE) {
    ESP_LOGD(TAG, "[remote] target=%d no-op change while phase=%d; ignoring", target, s_remote.phase);
    return;
  }

  ESP_LOGI(TAG,
           "[remote] request target=%d prev=%.2f new=%.2f phase=%d",
           target,
           previous_value,
           value_c,
           s_remote.phase);
  remote_queue_session(&next_session);
}

static void remote_reset_state(void)
{
  remote_cancel_timer(&s_remote.wait_lit_timer);
  remote_cancel_timer(&s_remote.pre_delay_timer);
  remote_cancel_timer(&s_remote.post_delay_timer);
  memset(&s_remote, 0, sizeof(s_remote));
  s_remote.phase = REMOTE_PHASE_IDLE;
  s_remote.cooling_anim_ctx.target = THERMOSTAT_TARGET_COOL;
  s_remote.heating_anim_ctx.target = THERMOSTAT_TARGET_HEAT;
}

static bool remote_update_target_value(thermostat_target_t target, float value_c)
{
  float *slot = (target == THERMOSTAT_TARGET_COOL) ? &s_remote.desired_cooling_c
                                                   : &s_remote.desired_heating_c;
  bool changed = fabsf(*slot - value_c) >= REMOTE_VALUE_EPSILON;
  *slot = value_c;
  if (target == THERMOSTAT_TARGET_COOL) {
    s_remote.desired_cooling_valid = true;
  } else {
    s_remote.desired_heating_valid = true;
  }
  return changed;
}

static void remote_prepare_session(remote_session_t *session)
{
  session->cooling_target_c = s_remote.desired_cooling_c;
  session->heating_target_c = s_remote.desired_heating_c;
  session->cooling_valid = s_remote.desired_cooling_valid;
  session->heating_valid = s_remote.desired_heating_valid;
}

static void remote_queue_session(const remote_session_t *session)
{
  switch (s_remote.phase) {
  case REMOTE_PHASE_IDLE:
    s_remote.current = *session;
    s_remote.has_pending = false;
    s_remote.awaiting_initial_delay = true;
    s_remote.initial_wake_consumed = backlight_manager_notify_interaction(BACKLIGHT_WAKE_REASON_REMOTE);
    s_remote.last_remote_serial = backlight_manager_get_interaction_serial();
    ESP_LOGI(TAG, "[remote] wake requested (consumed=%d)", s_remote.initial_wake_consumed);
    remote_start_wait_for_light();
    break;
  case REMOTE_PHASE_WAIT_LIT:
  case REMOTE_PHASE_PRE_DELAY:
    ESP_LOGD(TAG, "[remote] updated current session before animation");
    s_remote.current = *session;
    break;
  case REMOTE_PHASE_ANIMATING:
    s_remote.pending = *session;
    s_remote.has_pending = true;
    ESP_LOGI(TAG, "[remote] queued pending session during animation");
    break;
  case REMOTE_PHASE_POST_DELAY:
    s_remote.pending = *session;
    s_remote.has_pending = true;
    ESP_LOGI(TAG, "[remote] pending session arrived during hold; restarting immediately");
    remote_start_pending_session();
    break;
  default:
    break;
  }
}

static void remote_start_wait_for_light(void)
{
  s_remote.phase = REMOTE_PHASE_WAIT_LIT;
  if (backlight_manager_is_lit()) {
    ESP_LOGI(TAG, "[remote] backlight already lit; starting delay");
    remote_start_pre_delay();
    return;
  }
  remote_cancel_timer(&s_remote.wait_lit_timer);
  s_remote.wait_lit_timer = lv_timer_create(remote_wait_lit_cb, REMOTE_WAIT_LIT_POLL_MS, NULL);
}

static void remote_wait_lit_cb(lv_timer_t *timer)
{
  if (!backlight_manager_is_lit()) {
    return;
  }
  ESP_LOGI(TAG, "[remote] backlight lit; starting pre-delay");
  remote_cancel_timer(&s_remote.wait_lit_timer);
  remote_start_pre_delay();
}

static void remote_start_pre_delay(void)
{
  if (!s_remote.awaiting_initial_delay) {
    remote_start_animation();
    return;
  }
  s_remote.phase = REMOTE_PHASE_PRE_DELAY;
  remote_cancel_timer(&s_remote.pre_delay_timer);
  s_remote.pre_delay_timer = lv_timer_create(remote_pre_delay_cb, REMOTE_PRE_DELAY_MS, NULL);
  ESP_LOGI(TAG, "[remote] pre-delay scheduled (%d ms)", REMOTE_PRE_DELAY_MS);
}

static void remote_pre_delay_cb(lv_timer_t *timer)
{
  ESP_LOGI(TAG, "[remote] pre-delay complete");
  remote_cancel_timer(&s_remote.pre_delay_timer);
  remote_start_animation();
}

static void remote_start_animation(void)
{
  lv_obj_t *cool_track = thermostat_get_setpoint_track(THERMOSTAT_TARGET_COOL);
  lv_obj_t *heat_track = thermostat_get_setpoint_track(THERMOSTAT_TARGET_HEAT);
  if (cool_track == NULL || heat_track == NULL) {
    ESP_LOGW(TAG, "[remote] setpoint tracks unavailable; skipping animation");
    remote_finish_burst();
    return;
  }

  remote_poke_activity();
  s_remote.awaiting_initial_delay = false;
  s_remote.phase = REMOTE_PHASE_ANIMATING;
  s_remote.active_anim_count = 0;
  s_remote.cooling_anim_ctx.valid = s_remote.current.cooling_valid;
  s_remote.heating_anim_ctx.valid = s_remote.current.heating_valid;

  const float cool_start_temp = thermostat_temperature_from_y(lv_obj_get_y(cool_track));
  const float heat_start_temp = thermostat_temperature_from_y(lv_obj_get_y(heat_track));

  remote_start_temp_anim(&s_remote.cooling_anim_ctx, cool_start_temp, s_remote.current.cooling_target_c);
  remote_start_temp_anim(&s_remote.heating_anim_ctx, heat_start_temp, s_remote.current.heating_target_c);

  if (s_remote.active_anim_count == 0) {
    remote_handle_animation_complete();
  } else {
    ESP_LOGI(TAG,
             "[remote] animation started (targets cool=%.1f heat=%.1f)",
             s_remote.current.cooling_target_c,
             s_remote.current.heating_target_c);
  }
}

static void remote_start_pending_session(void)
{
  if (!s_remote.has_pending) {
    return;
  }
  s_remote.current = s_remote.pending;
  s_remote.has_pending = false;
  remote_cancel_timer(&s_remote.post_delay_timer);
  remote_start_animation();
}

static void remote_anim_exec_apply_temp(void *ctx, int32_t value)
{
  temp_anim_ctx_t *anim_ctx = (temp_anim_ctx_t *)ctx;
  float temp = (float)value / 10.0f;
  thermostat_apply_remote_temperature(anim_ctx->target, temp, anim_ctx->valid);
}

static void remote_start_anim(void *var, lv_anim_exec_xcb_t exec_cb, int32_t start, int32_t end)
{
  if (var == NULL || exec_cb == NULL) {
    return;
  }
  if (start == end) {
    exec_cb(var, end);
    return;
  }
  lv_anim_t anim;
  lv_anim_init(&anim);
  lv_anim_set_var(&anim, var);
  lv_anim_set_values(&anim, start, end);
  lv_anim_set_time(&anim, REMOTE_ANIMATION_MS);
  lv_anim_set_path_cb(&anim, lv_anim_path_ease_in_out);
  lv_anim_set_exec_cb(&anim, exec_cb);
  lv_anim_set_ready_cb(&anim, remote_animation_ready_cb);
  lv_anim_start(&anim);
  s_remote.active_anim_count++;
}

static void remote_start_temp_anim(temp_anim_ctx_t *ctx, float start_c, float target_c)
{
  remote_start_anim(ctx,
                    remote_anim_exec_apply_temp,
                    remote_temp_to_anim_value(start_c),
                    remote_temp_to_anim_value(target_c));
}

static void remote_animation_ready_cb(lv_anim_t *anim)
{
  if (s_remote.active_anim_count > 0) {
    s_remote.active_anim_count--;
  }
  if (s_remote.active_anim_count == 0 && s_remote.phase == REMOTE_PHASE_ANIMATING) {
    remote_handle_animation_complete();
  }
}

static void remote_handle_animation_complete(void)
{
  ESP_LOGI(TAG, "[remote] animation complete");
  thermostat_apply_remote_temperature(THERMOSTAT_TARGET_COOL,
                                      s_remote.current.cooling_target_c,
                                      s_remote.current.cooling_valid);
  thermostat_apply_remote_temperature(THERMOSTAT_TARGET_HEAT,
                                      s_remote.current.heating_target_c,
                                      s_remote.current.heating_valid);
  if (s_remote.has_pending) {
    remote_start_pending_session();
    return;
  }
  remote_start_post_delay();
}

static void remote_start_post_delay(void)
{
  s_remote.phase = REMOTE_PHASE_POST_DELAY;
  remote_cancel_timer(&s_remote.post_delay_timer);
  s_remote.post_delay_timer = lv_timer_create(remote_post_delay_cb, REMOTE_POST_DELAY_MS, NULL);
  ESP_LOGI(TAG, "[remote] hold started (%d ms)", REMOTE_POST_DELAY_MS);
}

static void remote_post_delay_cb(lv_timer_t *timer)
{
  ESP_LOGI(TAG, "[remote] hold complete");
  remote_cancel_timer(&s_remote.post_delay_timer);
  remote_schedule_sleep_if_needed();
  remote_finish_burst();
}

static void remote_cancel_timer(lv_timer_t **timer)
{
  if (timer == NULL || *timer == NULL) {
    return;
  }
  lv_timer_del(*timer);
  *timer = NULL;
}

static void remote_schedule_sleep_if_needed(void)
{
  if (!s_remote.initial_wake_consumed) {
    ESP_LOGI(TAG, "[remote] wake was not consumed; skipping auto-sleep");
    return;
  }
  uint32_t current_serial = backlight_manager_get_interaction_serial();
  if (current_serial != s_remote.last_remote_serial) {
    ESP_LOGI(TAG, "[remote] interaction detected during burst; skipping auto-sleep");
    return;
  }
  ESP_LOGI(TAG, "[remote] scheduling auto-sleep (%d ms)", REMOTE_SLEEP_TIMEOUT_MS);
  backlight_manager_schedule_remote_sleep(REMOTE_SLEEP_TIMEOUT_MS);
}

static void remote_finish_burst(void)
{
  s_remote.phase = REMOTE_PHASE_IDLE;
  s_remote.has_pending = false;
  s_remote.awaiting_initial_delay = true;
  s_remote.initial_wake_consumed = false;
  s_remote.active_anim_count = 0;
  remote_cancel_timer(&s_remote.wait_lit_timer);
  remote_cancel_timer(&s_remote.pre_delay_timer);
  remote_cancel_timer(&s_remote.post_delay_timer);
}

static void remote_poke_activity(void)
{
  (void)backlight_manager_notify_interaction(BACKLIGHT_WAKE_REASON_REMOTE);
  s_remote.last_remote_serial = backlight_manager_get_interaction_serial();
}

static int32_t remote_temp_to_anim_value(float value_c)
{
  return (int32_t)lroundf(value_c * 10.0f);
}
