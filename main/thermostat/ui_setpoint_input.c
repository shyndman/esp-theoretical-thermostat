#include "esp_log.h"
#include "thermostat/ui_setpoint_input.h"
#include "thermostat/ui_setpoint_view.h"
#include "thermostat/backlight_manager.h"
#include "thermostat/ui_entrance_anim.h"
#include "connectivity/mqtt_dataplane.h"

static lv_obj_t *g_setpoint_overlay = NULL;
static const char *TAG = "thermostat_touch";

static void thermostat_setpoint_overlay_event(lv_event_t *e);
static void thermostat_handle_touch_event(lv_event_code_t code, lv_coord_t screen_y);
static bool thermostat_y_in_stripe(lv_coord_t screen_y, thermostat_target_t target, lv_area_t *stripe_out);
static float thermostat_calculate_anchor_temperature(int current_y, thermostat_target_t target);
static void thermostat_apply_anchor_mode_drag(int current_y);
extern float thermostat_get_temperature_per_pixel(void);
static const char *thermostat_target_name(thermostat_target_t target);
static const char *thermostat_event_name(lv_event_code_t code);

float thermostat_clamp_cooling(float candidate, float heating_setpoint)
{
  const float min_gap = THERMOSTAT_TEMP_STEP_C + THERMOSTAT_HEAT_OVERRUN_C;
  float min_limit = heating_setpoint + min_gap;
  if (min_limit > THERMOSTAT_MAX_TEMP_C)
    min_limit = THERMOSTAT_MAX_TEMP_C;
  float clamped = candidate;
  if (clamped < min_limit)
    clamped = min_limit;
  return thermostat_clamp_temperature(clamped);
}

float thermostat_clamp_heating(float candidate, float cooling_setpoint)
{
  float max_limit = cooling_setpoint - (THERMOSTAT_TEMP_STEP_C + THERMOSTAT_COOL_OVERRUN_C);
  if (max_limit < THERMOSTAT_MIN_TEMP_C)
    max_limit = THERMOSTAT_MIN_TEMP_C;
  float clamped = candidate;
  if (clamped > max_limit)
    clamped = max_limit;
  return thermostat_clamp_temperature(clamped);
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
  lv_obj_set_size(g_setpoint_overlay, lv_pct(100), 1000);
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
  if (thermostat_entrance_anim_is_active())
  {
    return;
  }
  if (backlight_manager_notify_interaction(BACKLIGHT_WAKE_REASON_TOUCH)) {
    return;
  }
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

    // NEW: Detect if we should activate anchor mode (any stripe activates anchor)
    bool should_anchor = (in_cool || in_heat);

    if (desired != g_view_model.active_target)
    {
      thermostat_select_setpoint_target(desired);
      ESP_LOGI(TAG, "active target switched to %s", thermostat_target_name(desired));
    }

    // NEW: Configure anchor mode
    if (should_anchor) {
      g_view_model.anchor_mode_active = true;
      float current_temp = (desired == THERMOSTAT_TARGET_COOL) ?
                            g_view_model.cooling_setpoint_c :
                            g_view_model.heating_setpoint_c;
      g_view_model.anchor_temperature = current_temp;
      g_view_model.anchor_y = screen_y;
    } else {
      g_view_model.anchor_mode_active = false;
    }

    g_view_model.drag_active = true;
    ESP_LOGI(TAG, "drag started target=%s anchor_mode=%d", thermostat_target_name(g_view_model.active_target), g_view_model.anchor_mode_active);

    // NEW: Only apply touch if NOT in anchor mode
    if (!g_view_model.anchor_mode_active) {
      thermostat_apply_setpoint_touch(screen_y);
    }
    break;
  }
  case LV_EVENT_PRESSING:
    if (g_view_model.drag_active) {
      if (g_view_model.anchor_mode_active) {
        thermostat_apply_anchor_mode_drag(screen_y);
      } else {
        thermostat_apply_setpoint_touch(screen_y);
      }
    }
    break;
  case LV_EVENT_RELEASED:
  case LV_EVENT_PRESS_LOST:
    if (g_view_model.drag_active) {
      if (g_view_model.anchor_mode_active) {
        thermostat_apply_anchor_mode_drag(screen_y);
      } else {
        thermostat_apply_setpoint_touch(screen_y);
      }

      // NEW: Clean up anchor mode state
      g_view_model.anchor_mode_active = false;
      g_view_model.anchor_temperature = 0.0f;
      g_view_model.anchor_y = 0;

      g_view_model.drag_active = false;
      ESP_LOGI(TAG, "drag finished target=%s", thermostat_target_name(g_view_model.active_target));
      thermostat_commit_setpoints();
    }
    break;
  default:
    ESP_LOGW(TAG, "Unhandled setpoint overlay event=%s", thermostat_event_name(code));
    break;
  }
}

void thermostat_select_setpoint_target(thermostat_target_t target)
{
  // NEW: If anchor mode is active, transfer anchor to new target
  if (g_view_model.anchor_mode_active && target != g_view_model.active_target) {
    float new_anchor_temp = (target == THERMOSTAT_TARGET_COOL) ?
                            g_view_model.cooling_setpoint_c :
                            g_view_model.heating_setpoint_c;
    g_view_model.anchor_temperature = new_anchor_temp;
    // anchor_y remains unchanged to maintain drag continuity
  }

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

static float thermostat_calculate_anchor_temperature(int current_y, thermostat_target_t target)
{
  if (!g_view_model.anchor_mode_active) {
    // Delegate to existing conversion for normal mode
    return thermostat_temperature_from_y(current_y);
  }

  // Calculate proportional temperature change
  int y_delta = current_y - g_view_model.anchor_y;
  float temperature_delta = y_delta * thermostat_get_temperature_per_pixel();
  float new_temperature = g_view_model.anchor_temperature + temperature_delta;

  // Apply target-specific constraints
  if (target == THERMOSTAT_TARGET_COOL) {
    return thermostat_clamp_cooling(new_temperature, g_view_model.heating_setpoint_c);
  } else {
    return thermostat_clamp_heating(new_temperature, g_view_model.cooling_setpoint_c);
  }
}

static void thermostat_apply_anchor_mode_drag(int current_y)
{
  thermostat_target_t target = g_view_model.active_target;

  // Calculate new temperature using anchor mode logic
  float new_temp = thermostat_calculate_anchor_temperature(current_y, target);

  // Apply to state
  thermostat_slider_state_t state;
  thermostat_compute_state_from_temperature(new_temp, &state);
  thermostat_apply_state_to_target(target, &state);

  // Sync and update UI
  if (target == g_view_model.active_target) {
    thermostat_sync_active_slider_state(&state);
  }

  thermostat_update_setpoint_labels();
  thermostat_position_setpoint_labels();
  thermostat_update_track_geometry();
}

void thermostat_commit_setpoints(void)
{
  LV_LOG_INFO("Committing setpoints cooling=%.2f heating=%.2f",
              g_view_model.cooling_setpoint_c,
              g_view_model.heating_setpoint_c);
  esp_err_t err = mqtt_dataplane_publish_temperature_command(g_view_model.cooling_setpoint_c,
                                                            g_view_model.heating_setpoint_c);
  if (err != ESP_OK)
  {
    ESP_LOGE(TAG, "Failed to publish temperature_command (%s)", esp_err_to_name(err));
  }
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

void thermostat_apply_remote_temperature(thermostat_target_t target, float value_c, bool is_valid)
{
  // NEW: Remote updates should deactivate anchor mode
  if (g_view_model.anchor_mode_active) {
    g_view_model.anchor_mode_active = false;
    g_view_model.anchor_temperature = 0.0f;
    g_view_model.anchor_y = 0;
    ESP_LOGI(TAG, "anchor mode deactivated by remote update");
  }

  thermostat_slider_state_t state;
  thermostat_compute_state_from_temperature(value_c, &state);
  thermostat_apply_state_to_target(target, &state);
  if (target == THERMOSTAT_TARGET_COOL)
  {
    g_view_model.cooling_setpoint_c = state.setpoint;
    g_view_model.cooling_setpoint_valid = is_valid;
  }
  else
  {
    g_view_model.heating_setpoint_c = state.setpoint;
    g_view_model.heating_setpoint_valid = is_valid;
  }
  if (target == g_view_model.active_target)
  {
    thermostat_sync_active_slider_state(&state);
  }
  thermostat_update_setpoint_labels();
  thermostat_position_setpoint_labels();
  thermostat_update_track_geometry();
  thermostat_update_active_setpoint_styles();
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
    ESP_LOGW(TAG, "Unknown LVGL event code=%d", code);
    return "OTHER";
  }
}
