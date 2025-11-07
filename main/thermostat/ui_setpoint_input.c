#include <math.h>
#include "thermostat/ui_setpoint_input.h"
#include "thermostat/ui_setpoint_view.h"

lv_obj_t *g_track_touch_zone = NULL;

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

void thermostat_create_touch_zone(lv_obj_t *parent)
{
  g_track_touch_zone = lv_obj_create(parent);
  lv_obj_remove_style_all(g_track_touch_zone);
  lv_obj_clear_flag(g_track_touch_zone, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(g_track_touch_zone, lv_pct(100), thermostat_scale_length(1160));
  lv_obj_set_pos(g_track_touch_zone, 0, thermostat_scale_coord(120));
  lv_obj_set_style_bg_opa(g_track_touch_zone, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_add_flag(g_track_touch_zone, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(g_track_touch_zone, thermostat_track_touch_event, LV_EVENT_PRESSED, NULL);
  lv_obj_add_event_cb(g_track_touch_zone, thermostat_track_touch_event, LV_EVENT_PRESSING, NULL);
  lv_obj_add_event_cb(g_track_touch_zone, thermostat_track_touch_event, LV_EVENT_RELEASED, NULL);
  lv_obj_add_event_cb(g_track_touch_zone, thermostat_track_touch_event, LV_EVENT_PRESS_LOST, NULL);

  thermostat_update_track_geometry();
}

void thermostat_track_touch_event(lv_event_t *e)
{
  lv_event_code_t code = lv_event_get_code(e);
  lv_indev_t *indev = lv_event_get_indev(e);
  if (indev == NULL)
  {
    return;
  }
  lv_point_t point;
  lv_indev_get_point(indev, &point);

  if (code == LV_EVENT_PRESSED)
  {
    g_view_model.drag_active = false;
    g_view_model.pending_drag_active = true;
    g_view_model.pending_drag_start_y = point.y;
    thermostat_select_target_near(point.y);
  }
  else if (code == LV_EVENT_PRESSING)
  {
    if (g_view_model.pending_drag_active)
    {
      const int delta = LV_ABS(point.y - g_view_model.pending_drag_start_y);
      if (delta > 8)
      {
        g_view_model.pending_drag_active = false;
        g_view_model.drag_active = true;
      }
    }
    if (g_view_model.drag_active)
    {
      thermostat_handle_drag_sample(point.y);
    }
  }
  else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST)
  {
    if (g_view_model.drag_active)
    {
      thermostat_handle_drag_sample(point.y);
    }
    g_view_model.drag_active = false;
    g_view_model.pending_drag_active = false;
    thermostat_commit_setpoints();
  }
}

void thermostat_handle_setpoint_event(lv_event_t *e)
{
  const thermostat_target_t target = (thermostat_target_t)(intptr_t)lv_event_get_user_data(e);
  switch (lv_event_get_code(e))
  {
  case LV_EVENT_PRESSED:
    thermostat_select_setpoint_target(target);
    break;
  case LV_EVENT_RELEASED:
    thermostat_commit_setpoints();
    break;
  case LV_EVENT_LONG_PRESSED_REPEAT:
    thermostat_select_setpoint_target(target);
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

void thermostat_handle_drag_sample(int sample_y)
{
  int base_sample = thermostat_to_base_y(sample_y);
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

void thermostat_select_target_near(int sample_y)
{
  int base_sample = thermostat_to_base_y(sample_y);
  thermostat_slider_state_t cool_state;
  thermostat_slider_state_t heat_state;
  thermostat_compute_state_from_temperature(g_view_model.cooling_setpoint_c, &cool_state);
  thermostat_compute_state_from_temperature(g_view_model.heating_setpoint_c, &heat_state);

  int dist_cool = LV_ABS(base_sample - cool_state.label_y);
  int dist_heat = LV_ABS(base_sample - heat_state.label_y);
  thermostat_target_t desired = (dist_cool <= dist_heat) ? THERMOSTAT_TARGET_COOL : THERMOSTAT_TARGET_HEAT;

  if (desired != g_view_model.active_target)
  {
    thermostat_select_setpoint_target(desired);
  }
}

int thermostat_to_base_y(int screen_y)
{
  return (int)lrintf(screen_y / g_layout_scale);
}

lv_obj_t *thermostat_get_track_touch_zone(void)
{
  return g_track_touch_zone;
}
