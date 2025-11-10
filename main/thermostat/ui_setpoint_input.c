#include <math.h>
#include "esp_log.h"
#include "thermostat/ui_setpoint_input.h"
#include "thermostat/ui_setpoint_view.h"
#include "thermostat/backlight_manager.h"

static lv_obj_t *g_setpoint_overlay = NULL;
static const char *TAG = "thermostat_touch";

static void thermostat_setpoint_overlay_event(lv_event_t *e);
static void thermostat_handle_touch_event(lv_event_code_t code, lv_coord_t screen_y);
static bool thermostat_y_in_stripe(lv_coord_t screen_y, thermostat_target_t target, lv_area_t *stripe_out);
static const char *thermostat_target_name(thermostat_target_t target);
static const char *thermostat_event_name(lv_event_code_t code);

float thermostat_clamp_cooling(float candidate, float heating_setpoint)
{
  float min_gap = THERMOSTAT_TEMP_STEP_C + THERMOSTAT_HEAT_OVERRUN_C;
  float min_limit = heating_setpoint + min_gap;
  min_limit = ceilf(min_limit / THERMOSTAT_TEMP_STEP_C) * THERMOSTAT_TEMP_STEP_C;
  if (min_limit > THERMOSTAT_MAX_TEMP_C)
    min_limit = THERMOSTAT_MAX_TEMP_C;
  float rounded = thermostat_round_to_step(candidate);
  if (rounded < min_limit)
    rounded = min_limit;
  return thermostat_clamp_temperature(rounded);
}

float thermostat_clamp_heating(float candidate, float cooling_setpoint)
{
  float limit = cooling_setpoint - (THERMOSTAT_TEMP_STEP_C + THERMOSTAT_COOL_OVERRUN_C);
  float stepped_limit = floorf(limit / THERMOSTAT_TEMP_STEP_C) * THERMOSTAT_TEMP_STEP_C;
  if (stepped_limit < THERMOSTAT_MIN_TEMP_C)
    stepped_limit = THERMOSTAT_MIN_TEMP_C;
  float rounded = thermostat_round_to_step(candidate);
  if (rounded > cooling_setpoint)
    rounded = cooling_setpoint;
  if (rounded > stepped_limit)
    rounded = stepped_limit;
  return thermostat_clamp_temperature(rounded);
}

void thermostat_apply_state_to_target(thermostat_target_t target, const thermostat_slider_state_t *state)
{
  if (target == THERMOSTAT_TARGET_COOL)
  {
    g_view_model.cooling_setpoint_c = state->setpoint;
    g_view_model.cooling_track_y = state->track_y;
    g_view_model.cooling_track_height = state->track_height;
    g_view_model.cooling_label_y = state->label_y;
  }
  else
  {
    g_view_model.heating_setpoint_c = state->setpoint;
    g_view_model.heating_track_y = state->track_y;
    g_view_model.heating_track_height = state->track_height;
    g_view_model.heating_label_y = state->label_y;
  }
}

void thermostat_sync_active_slider_state(const thermostat_slider_state_t *state)
{
  g_view_model.track_y_position = state->track_y;
  g_view_model.slider_track_height = state->track_height;
  g_view_model.last_touch_y = state->track_y;
}

void thermostat_create_setpoint_overlay(lv_obj_t *parent)
{
  if (g_setpoint_overlay)
  {
    return;
  }
  g_setpoint_overlay = lv_obj_create(parent);
  lv_obj_remove_style_all(g_setpoint_overlay);
  lv_obj_clear_flag(g_setpoint_overlay, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(g_setpoint_overlay, lv_pct(100), 1160);
  lv_obj_set_pos(g_setpoint_overlay, 0, 120);
  lv_obj_set_style_bg_opa(g_setpoint_overlay, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_add_flag(g_setpoint_overlay, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(g_setpoint_overlay, LV_OBJ_FLAG_ADV_HITTEST);
  lv_obj_add_event_cb(g_setpoint_overlay, thermostat_setpoint_overlay_event, LV_EVENT_PRESSED, NULL);
  lv_obj_add_event_cb(g_setpoint_overlay, thermostat_setpoint_overlay_event, LV_EVENT_PRESSING, NULL);
  lv_obj_add_event_cb(g_setpoint_overlay, thermostat_setpoint_overlay_event, LV_EVENT_RELEASED, NULL);
  lv_obj_add_event_cb(g_setpoint_overlay, thermostat_setpoint_overlay_event, LV_EVENT_PRESS_LOST, NULL);

  thermostat_update_track_geometry();
}

static bool thermostat_y_in_stripe(lv_coord_t screen_y, thermostat_target_t target, lv_area_t *stripe_out)
{
  lv_area_t stripe;
  if (!thermostat_get_setpoint_stripe(target, &stripe))
  {
    return false;
  }
  if (stripe_out)
  {
    *stripe_out = stripe;
  }
  return (screen_y >= stripe.y1) && (screen_y <= stripe.y2);
}

static void thermostat_setpoint_overlay_event(lv_event_t *e)
{
  lv_event_code_t code = lv_event_get_code(e);
  lv_indev_t *indev = lv_event_get_indev(e);
  if (indev == NULL)
  {
    return;
  }
  backlight_manager_notify_interaction(BACKLIGHT_WAKE_REASON_TOUCH);
  lv_point_t point;
  lv_indev_get_point(indev, &point);
  ESP_LOGI(TAG, "overlay event=%s y=%d", thermostat_event_name(code), (int)point.y);
  thermostat_handle_touch_event(code, point.y);
}

static void thermostat_handle_touch_event(lv_event_code_t code, lv_coord_t screen_y)
{
  switch (code)
  {
  case LV_EVENT_PRESSED:
  {
    thermostat_target_t desired = g_view_model.active_target;
    lv_area_t cool_stripe = {0};
    lv_area_t heat_stripe = {0};
    bool in_cool = thermostat_y_in_stripe(screen_y, THERMOSTAT_TARGET_COOL, &cool_stripe);
    bool in_heat = thermostat_y_in_stripe(screen_y, THERMOSTAT_TARGET_HEAT, &heat_stripe);

    ESP_LOGI(TAG,
             "touch pressed y=%d in_cool=%d cool=[%d,%d] in_heat=%d heat=[%d,%d] active=%s",
             (int)screen_y,
             in_cool,
             (int)cool_stripe.y1,
             (int)cool_stripe.y2,
             in_heat,
             (int)heat_stripe.y1,
             (int)heat_stripe.y2,
             thermostat_target_name(g_view_model.active_target));

    if (in_cool)
    {
      desired = THERMOSTAT_TARGET_COOL;
    }
    else if (in_heat)
    {
      desired = THERMOSTAT_TARGET_HEAT;
    }
    if (desired != g_view_model.active_target)
    {
      thermostat_select_setpoint_target(desired);
      ESP_LOGI(TAG, "active target switched to %s", thermostat_target_name(desired));
    }
    g_view_model.drag_active = true;
    ESP_LOGI(TAG, "drag started target=%s", thermostat_target_name(g_view_model.active_target));
    thermostat_apply_setpoint_touch(screen_y);
    break;
  }
  case LV_EVENT_PRESSING:
    if (g_view_model.drag_active)
    {
      thermostat_apply_setpoint_touch(screen_y);
    }
    break;
  case LV_EVENT_RELEASED:
  case LV_EVENT_PRESS_LOST:
    if (g_view_model.drag_active)
    {
      thermostat_apply_setpoint_touch(screen_y);
      g_view_model.drag_active = false;
      ESP_LOGI(TAG, "drag finished target=%s", thermostat_target_name(g_view_model.active_target));
      thermostat_commit_setpoints();
    }
    break;
  default:
    break;
  }
}

void thermostat_select_setpoint_target(thermostat_target_t target)
{
  g_view_model.active_target = target;
  const float temp = (target == THERMOSTAT_TARGET_COOL) ? g_view_model.cooling_setpoint_c
                                                        : g_view_model.heating_setpoint_c;
  thermostat_slider_state_t state;
  thermostat_compute_state_from_temperature(temp, &state);
  thermostat_apply_state_to_target(target, &state);
  thermostat_sync_active_slider_state(&state);
  thermostat_position_setpoint_labels();
  thermostat_update_track_geometry();
  thermostat_update_active_setpoint_styles();
}

void thermostat_commit_setpoints(void)
{
  LV_LOG_INFO("Committing setpoints cooling=%.1f heating=%.1f",
              g_view_model.cooling_setpoint_c,
              g_view_model.heating_setpoint_c);
}

// Updates the active slider using the provided touch sample (tap or drag).
void thermostat_apply_setpoint_touch(int sample_y)
{
  int base_sample = sample_y;
  thermostat_slider_state_t state;
  thermostat_compute_state_from_y(base_sample, &state);

  if (g_view_model.active_target == THERMOSTAT_TARGET_COOL)
  {
    float clamped = thermostat_clamp_cooling(state.setpoint, g_view_model.heating_setpoint_c);
    thermostat_compute_state_from_temperature(clamped, &state);
    thermostat_apply_state_to_target(THERMOSTAT_TARGET_COOL, &state);
  }
  else
  {
    float clamped = thermostat_clamp_heating(state.setpoint, g_view_model.cooling_setpoint_c);
    thermostat_compute_state_from_temperature(clamped, &state);
    thermostat_apply_state_to_target(THERMOSTAT_TARGET_HEAT, &state);
  }

  thermostat_sync_active_slider_state(&state);
  thermostat_update_setpoint_labels();
  thermostat_position_setpoint_labels();
  thermostat_update_track_geometry();
}

lv_obj_t *thermostat_get_setpoint_overlay(void)
{
  return g_setpoint_overlay;
}

static const char *thermostat_target_name(thermostat_target_t target)
{
  return (target == THERMOSTAT_TARGET_COOL) ? "COOL" : "HEAT";
}

static const char *thermostat_event_name(lv_event_code_t code)
{
  switch (code)
  {
  case LV_EVENT_PRESSED:
    return "PRESSED";
  case LV_EVENT_PRESSING:
    return "PRESSING";
  case LV_EVENT_RELEASED:
    return "RELEASED";
  case LV_EVENT_PRESS_LOST:
    return "PRESS_LOST";
  default:
    return "OTHER";
  }
}
