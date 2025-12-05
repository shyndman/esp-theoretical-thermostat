#include <math.h>
#include "esp_log.h"
#include "thermostat/ui_setpoint_view.h"
#include "thermostat/ui_helpers.h"
#include "thermostat/ui_setpoint_input.h"
#include "thermostat/ui_actions.h"
#include "thermostat/ui_theme.h"

static lv_obj_t *g_setpoint_group = NULL;
static lv_obj_t *g_cooling_container = NULL;
static lv_obj_t *g_heating_container = NULL;
static lv_obj_t *g_cooling_label = NULL;
static lv_obj_t *g_cooling_fraction_label = NULL;
static lv_obj_t *g_heating_label = NULL;
static lv_obj_t *g_heating_fraction_label = NULL;
static lv_obj_t *g_cooling_track = NULL;
static lv_obj_t *g_heating_track = NULL;
static lv_obj_t *g_tick_overlay = NULL;
static const float k_slider_slope = (THERMOSTAT_IDEAL_LABEL_Y - THERMOSTAT_TRACK_TOP_Y) /
                                    (THERMOSTAT_IDEAL_TEMP_C - THERMOSTAT_MAX_TEMP_C);
static const float k_slider_intercept = THERMOSTAT_TRACK_TOP_Y - (k_slider_slope * THERMOSTAT_MAX_TEMP_C);
static const int k_track_min_y = (int)(THERMOSTAT_TRACK_TOP_Y + 0.5f);
static const int k_track_max_y = (int)((k_slider_slope * THERMOSTAT_MIN_TEMP_C + k_slider_intercept) + 0.5f);
static const char *thermostat_target_name_local(thermostat_target_t target);
static const char *TAG_STRIPE = "thermostat_stripe";

// Only use this helper when rendering human-facing text; all other code should
// work with the full-precision float values.
static int thermostat_round_tenths_for_display(float value)
{
  return (int)lroundf(value * 10.0f);
}


float thermostat_clamp_temperature(float value)
{
  if (value < THERMOSTAT_MIN_TEMP_C)
    return THERMOSTAT_MIN_TEMP_C;
  if (value > THERMOSTAT_MAX_TEMP_C)
    return THERMOSTAT_MAX_TEMP_C;
  return value;
}

int thermostat_clamp_track_y(int y)
{
  if (y < k_track_min_y)
    return k_track_min_y;
  if (y > k_track_max_y)
    return k_track_max_y;
  return y;
}

float thermostat_temperature_from_y(int track_y)
{
  float raw = (track_y - k_slider_intercept) / k_slider_slope;
  return thermostat_clamp_temperature(raw);
}

int thermostat_track_y_from_temperature(float temp)
{
  float clamped = thermostat_clamp_temperature(temp);
  float raw = (k_slider_slope * clamped) + k_slider_intercept;
  int y = (int)roundf(raw);
  return thermostat_clamp_track_y(y);
}

int thermostat_compute_label_y(int track_y)
{
  int label_y = (int)lrintf(track_y - THERMOSTAT_LABEL_OFFSET);
  if (label_y < 120)
    label_y = 120;
  return label_y;
}

int thermostat_compute_track_height(int track_y)
{
  int height = (int)(THERMOSTAT_TRACK_PANEL_HEIGHT - track_y);
  if (height < 0)
    height = 0;
  return height;
}

void thermostat_compute_state_from_temperature(float temp, thermostat_slider_state_t *state)
{
  state->setpoint = thermostat_clamp_temperature(temp);
  state->track_y = thermostat_track_y_from_temperature(state->setpoint);
  state->track_height = thermostat_compute_track_height(state->track_y);
  state->label_y = thermostat_compute_label_y(state->track_y);
}

void thermostat_compute_state_from_y(int sample_y, thermostat_slider_state_t *state)
{
  int clamped_y = thermostat_clamp_track_y(sample_y);
  float temp = thermostat_temperature_from_y(clamped_y);
  thermostat_compute_state_from_temperature(temp, state);
}

void thermostat_create_tracks(lv_obj_t *parent)
{
  g_cooling_track = thermostat_setpoint_create_track(parent, lv_color_hex(THERMOSTAT_COLOR_COOL));
  g_heating_track = thermostat_setpoint_create_track(parent, lv_color_hex(THERMOSTAT_COLOR_HEAT));
  thermostat_update_track_geometry();
}

void thermostat_create_setpoint_group(lv_obj_t *parent)
{
  g_setpoint_group = lv_obj_create(parent);
  thermostat_ui_reset_container(g_setpoint_group);
  lv_obj_add_flag(g_setpoint_group, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  lv_obj_set_width(g_setpoint_group, lv_pct(100));
  lv_obj_set_height(g_setpoint_group, LV_SIZE_CONTENT);
  lv_obj_set_pos(g_setpoint_group, 0, g_view_model.setpoint_group_y);
  lv_obj_set_style_pad_left(g_setpoint_group, 38, LV_PART_MAIN);
  lv_obj_set_style_pad_right(g_setpoint_group, 38, LV_PART_MAIN);
  lv_obj_set_layout(g_setpoint_group, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(g_setpoint_group, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(g_setpoint_group, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);

  const thermostat_setpoint_container_config_t cooling_container_cfg = {
    .main_place = LV_FLEX_ALIGN_START,
    .cross_place = LV_FLEX_ALIGN_END,
    .track_place = LV_FLEX_ALIGN_END,
    .width = 260,
    .height = LV_SIZE_CONTENT,
    .pad_column = 0,
    .pad_left = false,
    .pad_right = true,
    .pad_right_px = 12,
  };
  g_cooling_container = thermostat_setpoint_create_container(g_setpoint_group, &cooling_container_cfg);

  const thermostat_setpoint_label_config_t cooling_label_cfg = {
    .font = g_fonts.setpoint_primary,
    .color = lv_color_hex(THERMOSTAT_COLOR_NEUTRAL),
    .translate_x = 0,
    .translate_y = 0,
  };
  g_cooling_label = thermostat_setpoint_create_label(g_cooling_container, &cooling_label_cfg);

  const thermostat_setpoint_label_config_t cooling_fraction_cfg = {
    .font = g_fonts.setpoint_secondary,
    .color = lv_color_hex(THERMOSTAT_COLOR_NEUTRAL),
    .translate_x = -30,
    .translate_y = -2,
  };
  g_cooling_fraction_label = thermostat_setpoint_create_label(g_cooling_container, &cooling_fraction_cfg);

  const thermostat_setpoint_container_config_t heating_container_cfg = {
    .main_place = LV_FLEX_ALIGN_END,
    .cross_place = LV_FLEX_ALIGN_END,
    .track_place = LV_FLEX_ALIGN_END,
    .width = 260,
    .height = LV_SIZE_CONTENT,
    .pad_column = 0,
    .pad_left = true,
    .pad_left_px = 12,
    .pad_right = false,
  };
  g_heating_container = thermostat_setpoint_create_container(g_setpoint_group, &heating_container_cfg);

  const thermostat_setpoint_label_config_t heating_label_cfg = {
    .font = g_fonts.setpoint_primary,
    .color = lv_color_hex(THERMOSTAT_COLOR_HEAT),
    .translate_x = 29,
    .translate_y = 0,
  };
  g_heating_label = thermostat_setpoint_create_label(g_heating_container, &heating_label_cfg);

  const thermostat_setpoint_label_config_t heating_fraction_cfg = {
    .font = g_fonts.setpoint_secondary,
    .color = lv_color_hex(THERMOSTAT_COLOR_HEAT),
    .translate_x = 0,
    .translate_y = -2,
  };
  g_heating_fraction_label = thermostat_setpoint_create_label(g_heating_container, &heating_fraction_cfg);

  thermostat_update_setpoint_labels();
  thermostat_update_active_setpoint_styles();
  thermostat_update_track_geometry();
  thermostat_position_setpoint_labels();
}

void thermostat_update_setpoint_labels(void)
{
  if (g_cooling_label == NULL ||
      g_cooling_fraction_label == NULL ||
      g_heating_label == NULL ||
      g_heating_fraction_label == NULL)
  {
    return;
  }

  const thermostat_setpoint_label_pair_t cooling_labels = {
    .whole_label = g_cooling_label,
    .fraction_label = g_cooling_fraction_label,
    .color_valid = lv_color_hex(THERMOSTAT_COLOR_COOL),
  };
  thermostat_setpoint_update_value_labels(&cooling_labels,
                                          g_view_model.cooling_setpoint_valid,
                                          g_view_model.cooling_setpoint_c);

  const thermostat_setpoint_label_pair_t heating_labels = {
    .whole_label = g_heating_label,
    .fraction_label = g_heating_fraction_label,
    .color_valid = lv_color_hex(THERMOSTAT_COLOR_HEAT),
  };
  thermostat_setpoint_update_value_labels(&heating_labels,
                                          g_view_model.heating_setpoint_valid,
                                          g_view_model.heating_setpoint_c);

  thermostat_update_active_setpoint_styles();
}

void thermostat_update_active_setpoint_styles(void)
{
  const bool cooling_active = g_view_model.active_target == THERMOSTAT_TARGET_COOL;
  const bool heating_active = g_view_model.active_target == THERMOSTAT_TARGET_HEAT;
  const lv_color_t color_cool = lv_color_hex(THERMOSTAT_COLOR_COOL);
  const lv_color_t color_heat = lv_color_hex(THERMOSTAT_COLOR_HEAT);
  const lv_color_t color_neutral = lv_color_hex(THERMOSTAT_COLOR_NEUTRAL);
  const thermostat_setpoint_active_style_t cooling_style = {
    .container = g_cooling_container,
    .whole_label = g_cooling_label,
    .fraction_label = g_cooling_fraction_label,
    .track = g_cooling_track,
    .is_active = cooling_active,
    .setpoint_valid = g_view_model.cooling_setpoint_valid,
    .color_active = color_cool,
    .color_inactive = color_neutral,
    .label_opa_active = THERMOSTAT_OPA_LABEL_ACTIVE,
    .label_opa_inactive = THERMOSTAT_OPA_LABEL_INACTIVE_COOL,
    .track_opa_active = LV_OPA_COVER,
    .track_opa_inactive = THERMOSTAT_OPA_TRACK_INACTIVE_COOL,
  };
  thermostat_setpoint_apply_active_styles(&cooling_style);

  const thermostat_setpoint_active_style_t heating_style = {
    .container = g_heating_container,
    .whole_label = g_heating_label,
    .fraction_label = g_heating_fraction_label,
    .track = g_heating_track,
    .is_active = heating_active,
    .setpoint_valid = g_view_model.heating_setpoint_valid,
    .color_active = color_heat,
    .color_inactive = color_heat,
    .label_opa_active = THERMOSTAT_OPA_LABEL_ACTIVE,
    .label_opa_inactive = THERMOSTAT_OPA_LABEL_INACTIVE_HEAT,
    .track_opa_active = LV_OPA_COVER,
    .track_opa_inactive = THERMOSTAT_OPA_TRACK_INACTIVE_HEAT,
  };
  thermostat_setpoint_apply_active_styles(&heating_style);

  thermostat_update_action_bar_visuals();
}

void thermostat_format_setpoint(float value, char *whole_buf, size_t whole_buf_sz,
                                char *fraction_buf, size_t fraction_buf_sz)
{
  const int tenths = thermostat_round_tenths_for_display(value);
  const int whole = tenths / 10;
  int fraction = tenths % 10;
  if (fraction < 0)
  {
    fraction = -fraction;
  }

  lv_snprintf(whole_buf, whole_buf_sz, "%d%s", whole, THERMOSTAT_SYMBOL_DEG);
  lv_snprintf(fraction_buf, fraction_buf_sz, ".%d", fraction);
}

void thermostat_position_setpoint_labels(void)
{
  if (g_setpoint_group == NULL || g_cooling_container == NULL || g_heating_container == NULL)
  {
    return;
  }

  int base = LV_MIN(g_view_model.cooling_label_y, g_view_model.heating_label_y);
  g_view_model.setpoint_group_y = base;
  lv_obj_set_y(g_setpoint_group, base);
  lv_obj_set_style_translate_y(g_cooling_container,
                               g_view_model.cooling_label_y - base,
                               LV_PART_MAIN);
  lv_obj_set_style_translate_y(g_heating_container,
                               g_view_model.heating_label_y - base,
                               LV_PART_MAIN);

}

bool thermostat_get_setpoint_stripe(thermostat_target_t target, lv_area_t *stripe)
{
  if (stripe == NULL)
  {
    return false;
  }

  lv_obj_t *container = (target == THERMOSTAT_TARGET_COOL) ? g_cooling_container : g_heating_container;
  if (container == NULL)
  {
    return false;
  }

  lv_disp_t *disp = lv_obj_get_disp(container);
  if (disp == NULL)
  {
    return false;
  }

  lv_area_t coords;
  lv_obj_get_coords(container, &coords);
  lv_coord_t hor = lv_disp_get_hor_res(disp);

  stripe->x1 = 0;
  stripe->x2 = hor - 1;
  stripe->y1 = coords.y1;
  stripe->y2 = coords.y2;
  ESP_LOGI(TAG_STRIPE,
           "stripe target=%s x=[%d,%d] y=[%d,%d]",
           thermostat_target_name_local(target),
           (int)stripe->x1,
           (int)stripe->x2,
           (int)stripe->y1,
           (int)stripe->y2);
  return true;
}

lv_obj_t *thermostat_get_setpoint_track(thermostat_target_t target)
{
  return (target == THERMOSTAT_TARGET_COOL) ? g_cooling_track : g_heating_track;
}

lv_obj_t *thermostat_get_setpoint_group(void)
{
  return g_setpoint_group;
}

lv_obj_t *thermostat_get_setpoint_container(thermostat_target_t target)
{
  return (target == THERMOSTAT_TARGET_COOL) ? g_cooling_container : g_heating_container;
}

lv_obj_t *thermostat_get_setpoint_label(thermostat_target_t target)
{
  return (target == THERMOSTAT_TARGET_COOL) ? g_cooling_label : g_heating_label;
}

static const char *thermostat_target_name_local(thermostat_target_t target)
{
  return (target == THERMOSTAT_TARGET_COOL) ? "COOL" : "HEAT";
}

// Number of tick marks: every 0.5°C from MIN to MAX inclusive
#define THERMOSTAT_TICK_COUNT \
  ((int)((THERMOSTAT_MAX_TEMP_C - THERMOSTAT_MIN_TEMP_C) / 0.5f) + 1)

// Static line point storage: each tick uses a pair of points
static lv_point_precise_t g_tick_points[THERMOSTAT_TICK_COUNT][2];

static bool thermostat_is_whole_degree(float temp)
{
  // Check if the temperature is a whole degree (no fractional part)
  float frac = temp - (float)((int)temp);
  return (frac < 0.01f) || (frac > 0.99f);
}

void thermostat_create_tick_overlay(lv_obj_t *parent)
{
  if (g_tick_overlay != NULL)
  {
    return;
  }

  // Create the overlay container
  g_tick_overlay = lv_obj_create(parent);
  lv_obj_remove_style_all(g_tick_overlay);
  lv_obj_clear_flag(g_tick_overlay, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(g_tick_overlay, LV_OBJ_FLAG_OVERFLOW_VISIBLE);

  // Position and size: covers the full track span plus padding for stroke thickness
  lv_obj_set_width(g_tick_overlay, THERMOSTAT_TICK_OVERLAY_WIDTH);
  int stroke_pad = THERMOSTAT_TICK_WHOLE_STROKE / 2;
  int track_height = k_track_max_y - k_track_min_y;
  lv_obj_set_height(g_tick_overlay, track_height + THERMOSTAT_TICK_WHOLE_STROKE);
  lv_obj_set_y(g_tick_overlay, (int)THERMOSTAT_TRACK_TOP_Y - stroke_pad);

  // Populate with line children for each 0.5°C increment
  for (int i = 0; i < THERMOSTAT_TICK_COUNT; i++)
  {
    float temp = THERMOSTAT_MIN_TEMP_C + (i * 0.5f);
    int y = thermostat_track_y_from_temperature(temp) - k_track_min_y + stroke_pad;
    bool is_whole = thermostat_is_whole_degree(temp);
    int tick_width = is_whole ? THERMOSTAT_TICK_WHOLE_WIDTH : THERMOSTAT_TICK_HALF_WIDTH;

    // Line runs horizontally at this Y position
    g_tick_points[i][0].x = 0;
    g_tick_points[i][0].y = y;
    g_tick_points[i][1].x = tick_width;
    g_tick_points[i][1].y = y;

    lv_obj_t *line = lv_line_create(g_tick_overlay);
    lv_line_set_points(line, g_tick_points[i], 2);
    lv_obj_add_style(line, is_whole ? &g_style_tick_whole : &g_style_tick_half, LV_PART_MAIN);
  }

  // Initial translation based on active target
  thermostat_update_tick_overlay_position();
}

void thermostat_update_tick_overlay_position(void)
{
  if (g_tick_overlay == NULL)
  {
    return;
  }

  lv_disp_t *disp = lv_obj_get_disp(g_tick_overlay);
  if (disp == NULL)
  {
    return;
  }
  lv_coord_t screen_width = lv_disp_get_hor_res(disp);

  // Ticks attach to the screen edges:
  // - Cooling: left edge of screen (x=0)
  // - Heating: right edge of screen
  lv_coord_t x_cooling = 0;
  lv_coord_t x_heating = screen_width - THERMOSTAT_TICK_OVERLAY_WIDTH;

  lv_coord_t target_x = (g_view_model.active_target == THERMOSTAT_TARGET_COOL)
                            ? x_cooling
                            : x_heating;

  lv_obj_set_x(g_tick_overlay, target_x);
  lv_obj_invalidate(g_tick_overlay);
}

void thermostat_update_track_geometry(void)
{
  if (g_cooling_track)
  {
    lv_obj_set_y(g_cooling_track, g_view_model.cooling_track_y);
    lv_obj_set_height(g_cooling_track, g_view_model.cooling_track_height);
  }
  if (g_heating_track)
  {
    lv_obj_set_y(g_heating_track, g_view_model.heating_track_y);
    lv_obj_set_height(g_heating_track, g_view_model.heating_track_height);
  }
}
