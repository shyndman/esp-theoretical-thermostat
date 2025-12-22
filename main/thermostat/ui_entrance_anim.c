#include "thermostat/ui_entrance_anim.h"

#include "esp_log.h"
#include "thermostat/ui_actions.h"
#include "thermostat/ui_animation_timing.h"
#include "thermostat/ui_setpoint_view.h"
#include "thermostat/ui_top_bar.h"

typedef struct
{
  lv_obj_t *track;
  lv_coord_t bottom;
  lv_coord_t target_height;
} track_anim_ctx_t;

typedef struct
{
  lv_obj_t *obj;
  lv_opa_t target_opa;
} entrance_label_state_t;

typedef struct
{
  bool pending;
  bool active;
  thermostat_entrance_anim_complete_cb_t complete_cb;
  void *complete_ctx;
} entrance_anim_state_t;

static entrance_anim_state_t s_state = {0};
static track_anim_ctx_t s_cooling_track = {0};
static track_anim_ctx_t s_heating_track = {0};
static entrance_label_state_t s_cooling_label = {0};
static entrance_label_state_t s_cooling_fraction_label = {0};
static entrance_label_state_t s_heating_label = {0};
static entrance_label_state_t s_heating_fraction_label = {0};
static const char *TAG = "ui_entrance";

static void entrance_set_opa(lv_obj_t *obj, lv_opa_t opa);
static void entrance_track_exec_cb(void *var, int32_t value);
static void entrance_opa_exec_cb(void *var, int32_t value);
static void entrance_start_track_anim(track_anim_ctx_t *ctx, uint32_t delay_ms, uint32_t duration_ms);
static void entrance_start_fade(lv_obj_t *obj,
                                uint32_t delay_ms,
                                uint32_t duration_ms,
                                lv_opa_t target_opa,
                                lv_anim_ready_cb_t ready_cb);
static void entrance_complete(void);
static void entrance_action_bar_ready_cb(lv_anim_t *anim);

void thermostat_entrance_anim_prepare(void)
{
  s_state.pending = true;
  s_state.active = false;

  lv_obj_t *weather_group = thermostat_get_weather_group();
  lv_obj_t *hvac_group = thermostat_get_hvac_status_group();
  lv_obj_t *room_group = thermostat_get_room_group();
  entrance_set_opa(weather_group, LV_OPA_TRANSP);
  entrance_set_opa(hvac_group, LV_OPA_TRANSP);
  entrance_set_opa(room_group, LV_OPA_TRANSP);

  lv_obj_t *cool_track = thermostat_get_setpoint_track(THERMOSTAT_TARGET_COOL);
  lv_obj_t *heat_track = thermostat_get_setpoint_track(THERMOSTAT_TARGET_HEAT);
  if (cool_track)
  {
    s_cooling_track.track = cool_track;
    s_cooling_track.target_height = lv_obj_get_height(cool_track);
    s_cooling_track.bottom = lv_obj_get_y(cool_track) + s_cooling_track.target_height;
    lv_obj_set_height(cool_track, 0);
    lv_obj_set_y(cool_track, s_cooling_track.bottom);
  }
  if (heat_track)
  {
    s_heating_track.track = heat_track;
    s_heating_track.target_height = lv_obj_get_height(heat_track);
    s_heating_track.bottom = lv_obj_get_y(heat_track) + s_heating_track.target_height;
    lv_obj_set_height(heat_track, 0);
    lv_obj_set_y(heat_track, s_heating_track.bottom);
  }

  s_cooling_label.obj = thermostat_get_cooling_label();
  s_cooling_label.target_opa = LV_OPA_COVER;
  if (s_cooling_label.obj)
  {
    s_cooling_label.target_opa = lv_obj_get_style_opa(s_cooling_label.obj, LV_PART_MAIN);
  }
  entrance_set_opa(s_cooling_label.obj, LV_OPA_TRANSP);

  s_cooling_fraction_label.obj = thermostat_get_cooling_fraction_label();
  s_cooling_fraction_label.target_opa = LV_OPA_COVER;
  if (s_cooling_fraction_label.obj)
  {
    s_cooling_fraction_label.target_opa =
        lv_obj_get_style_opa(s_cooling_fraction_label.obj, LV_PART_MAIN);
  }
  entrance_set_opa(s_cooling_fraction_label.obj, LV_OPA_TRANSP);

  s_heating_label.obj = thermostat_get_heating_label();
  s_heating_label.target_opa = LV_OPA_COVER;
  if (s_heating_label.obj)
  {
    s_heating_label.target_opa = lv_obj_get_style_opa(s_heating_label.obj, LV_PART_MAIN);
  }
  entrance_set_opa(s_heating_label.obj, LV_OPA_TRANSP);

  s_heating_fraction_label.obj = thermostat_get_heating_fraction_label();
  s_heating_fraction_label.target_opa = LV_OPA_COVER;
  if (s_heating_fraction_label.obj)
  {
    s_heating_fraction_label.target_opa =
        lv_obj_get_style_opa(s_heating_fraction_label.obj, LV_PART_MAIN);
  }
  entrance_set_opa(s_heating_fraction_label.obj, LV_OPA_TRANSP);

  entrance_set_opa(thermostat_get_mode_icon(), LV_OPA_TRANSP);
  entrance_set_opa(thermostat_get_power_icon(), LV_OPA_TRANSP);
  entrance_set_opa(thermostat_get_fan_icon(), LV_OPA_TRANSP);
}

void thermostat_entrance_anim_start(void)
{
  if (s_state.active)
  {
    return;
  }

  s_state.pending = false;
  s_state.active = true;

  entrance_start_fade(thermostat_get_weather_group(),
                      0,
                      THERMOSTAT_ANIM_TOP_BAR_FADE_MS,
                      LV_OPA_COVER,
                      NULL);
  entrance_start_fade(thermostat_get_hvac_status_group(),
                      THERMOSTAT_ANIM_TOP_BAR_STAGGER_MS,
                      THERMOSTAT_ANIM_TOP_BAR_FADE_MS,
                      LV_OPA_COVER,
                      NULL);
  entrance_start_fade(thermostat_get_room_group(),
                      THERMOSTAT_ANIM_TOP_BAR_STAGGER_MS * 2,
                      THERMOSTAT_ANIM_TOP_BAR_FADE_MS,
                      LV_OPA_COVER,
                      NULL);

  entrance_start_track_anim(&s_cooling_track, 0, THERMOSTAT_ANIM_TRACK_COOL_GROW_MS);
  entrance_start_track_anim(&s_heating_track,
                            THERMOSTAT_ANIM_TRACK_HEAT_DELAY_MS,
                            THERMOSTAT_ANIM_TRACK_HEAT_GROW_MS);

  entrance_start_fade(s_cooling_label.obj,
                      THERMOSTAT_ANIM_TRACK_COOL_END_MS,
                      THERMOSTAT_ANIM_LABEL_FADE_MS,
                      s_cooling_label.target_opa,
                      NULL);
  entrance_start_fade(s_cooling_fraction_label.obj,
                      THERMOSTAT_ANIM_TRACK_COOL_END_MS +
                          THERMOSTAT_ANIM_LABEL_FRACTION_DELAY_MS,
                      THERMOSTAT_ANIM_LABEL_FADE_MS,
                      s_cooling_fraction_label.target_opa,
                      NULL);
  entrance_start_fade(s_heating_label.obj,
                      THERMOSTAT_ANIM_TRACK_HEAT_END_MS,
                      THERMOSTAT_ANIM_LABEL_FADE_MS,
                      s_heating_label.target_opa,
                      NULL);
  entrance_start_fade(s_heating_fraction_label.obj,
                      THERMOSTAT_ANIM_TRACK_HEAT_END_MS +
                          THERMOSTAT_ANIM_LABEL_FRACTION_DELAY_MS,
                      THERMOSTAT_ANIM_LABEL_FADE_MS,
                      s_heating_fraction_label.target_opa,
                      NULL);

  const uint32_t action_start = THERMOSTAT_ANIM_TRACK_END_MS +
                                THERMOSTAT_ANIM_LABEL_FADE_MS +
                                THERMOSTAT_ANIM_LABEL_FRACTION_DELAY_MS;
  entrance_start_fade(thermostat_get_mode_icon(),
                      action_start,
                      THERMOSTAT_ANIM_ACTION_BAR_FADE_MS,
                      LV_OPA_COVER,
                      NULL);
  entrance_start_fade(thermostat_get_power_icon(),
                      action_start + THERMOSTAT_ANIM_ACTION_BAR_STAGGER_MS,
                      THERMOSTAT_ANIM_ACTION_BAR_FADE_MS,
                      LV_OPA_COVER,
                      NULL);
  entrance_start_fade(thermostat_get_fan_icon(),
                      action_start + THERMOSTAT_ANIM_ACTION_BAR_STAGGER_MS * 2,
                      THERMOSTAT_ANIM_ACTION_BAR_FADE_MS,
                      LV_OPA_COVER,
                      entrance_action_bar_ready_cb);

  if (!thermostat_get_fan_icon())
  {
    ESP_LOGW(TAG, "Fan icon missing; ending entrance animation immediately");
    entrance_complete();
  }
}

bool thermostat_entrance_anim_is_active(void)
{
  return s_state.pending || s_state.active;
}

void thermostat_entrance_anim_set_complete_cb(thermostat_entrance_anim_complete_cb_t cb, void *user_ctx)
{
  s_state.complete_cb = cb;
  s_state.complete_ctx = user_ctx;
}

static void entrance_set_opa(lv_obj_t *obj, lv_opa_t opa)
{
  if (obj)
  {
    lv_obj_set_style_opa(obj, opa, LV_PART_MAIN);
  }
}

static void entrance_track_exec_cb(void *var, int32_t value)
{
  track_anim_ctx_t *ctx = (track_anim_ctx_t *)var;
  if (!ctx || !ctx->track)
  {
    return;
  }

  lv_coord_t height = (lv_coord_t)value;
  lv_obj_set_height(ctx->track, height);
  lv_obj_set_y(ctx->track, ctx->bottom - height);
}

static void entrance_opa_exec_cb(void *var, int32_t value)
{
  lv_obj_t *obj = (lv_obj_t *)var;
  if (obj)
  {
    lv_obj_set_style_opa(obj, (lv_opa_t)value, LV_PART_MAIN);
  }
}

static void entrance_start_track_anim(track_anim_ctx_t *ctx, uint32_t delay_ms, uint32_t duration_ms)
{
  if (!ctx || !ctx->track)
  {
    return;
  }

  lv_anim_t anim;
  lv_anim_init(&anim);
  lv_anim_set_var(&anim, ctx);
  lv_anim_set_exec_cb(&anim, entrance_track_exec_cb);
  lv_anim_set_values(&anim, 0, ctx->target_height);
  lv_anim_set_time(&anim, duration_ms);
  lv_anim_set_delay(&anim, delay_ms);
  lv_anim_set_path_cb(&anim, lv_anim_path_custom_bezier3);
  LV_ANIM_SET_EASE_OUT_QUINT(&anim);
  lv_anim_start(&anim);
}

static void entrance_start_fade(lv_obj_t *obj,
                                uint32_t delay_ms,
                                uint32_t duration_ms,
                                lv_opa_t target_opa,
                                lv_anim_ready_cb_t ready_cb)
{
  if (!obj)
  {
    return;
  }

  lv_anim_t anim;
  lv_anim_init(&anim);
  lv_anim_set_var(&anim, obj);
  lv_anim_set_exec_cb(&anim, entrance_opa_exec_cb);
  lv_anim_set_values(&anim, lv_obj_get_style_opa(obj, LV_PART_MAIN), target_opa);
  lv_anim_set_time(&anim, duration_ms);
  lv_anim_set_delay(&anim, delay_ms);
  lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
  if (ready_cb)
  {
    lv_anim_set_ready_cb(&anim, ready_cb);
  }
  lv_anim_start(&anim);
}

static void entrance_complete(void)
{
  s_state.active = false;
  if (s_state.complete_cb)
  {
    s_state.complete_cb(s_state.complete_ctx);
  }
}

static void entrance_action_bar_ready_cb(lv_anim_t *anim)
{
  LV_UNUSED(anim);
  entrance_complete();
}
