#include <math.h>
#include "esp_log.h"
#include "thermostat/ui_setpoint_view.h"
#include "thermostat/ui_setpoint_input.h"
#include "thermostat/ui_actions.h"

static lv_obj_t *g_setpoint_group = NULL;
static lv_obj_t *g_cooling_container = NULL;
static lv_obj_t *g_heating_container = NULL;
static lv_obj_t *g_cooling_label = NULL;
static lv_obj_t *g_cooling_fraction_label = NULL;
static lv_obj_t *g_heating_label = NULL;
static lv_obj_t *g_heating_fraction_label = NULL;
static lv_obj_t *g_cooling_track = NULL;
static lv_obj_t *g_heating_track = NULL;
static const float k_slider_slope = (THERMOSTAT_IDEAL_LABEL_Y - THERMOSTAT_TRACK_TOP_Y) /
                                    (THERMOSTAT_IDEAL_TEMP_C - THERMOSTAT_MAX_TEMP_C);
static const float k_slider_intercept = THERMOSTAT_TRACK_TOP_Y - (k_slider_slope * THERMOSTAT_MAX_TEMP_C);
static const int k_track_min_y = (int)(THERMOSTAT_TRACK_TOP_Y + 0.5f);
static const int k_track_max_y = (int)((k_slider_slope * THERMOSTAT_MIN_TEMP_C + k_slider_intercept) + 0.5f);
static const char *thermostat_target_name_local(thermostat_target_t target);
static const char *TAG_STRIPE = "thermostat_stripe";


float thermostat_clamp_temperature(float value)
{
  if (value < THERMOSTAT_MIN_TEMP_C)
    return THERMOSTAT_MIN_TEMP_C;
  if (value > THERMOSTAT_MAX_TEMP_C)
    return THERMOSTAT_MAX_TEMP_C;
  return value;
}

float thermostat_round_to_step(float value)
{
  return roundf(value / THERMOSTAT_TEMP_STEP_C) * THERMOSTAT_TEMP_STEP_C;
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
  return thermostat_clamp_temperature(thermostat_round_to_step(raw));
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
  state->setpoint = thermostat_clamp_temperature(thermostat_round_to_step(temp));
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
  g_cooling_track = lv_obj_create(parent);
  lv_obj_remove_style_all(g_cooling_track);
  lv_obj_clear_flag(g_cooling_track, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_width(g_cooling_track, lv_pct(100));
  lv_obj_set_style_bg_color(g_cooling_track, lv_color_hex(THERMOSTAT_COLOR_COOL), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(g_cooling_track, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(g_cooling_track, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(g_cooling_track, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(g_cooling_track, 0, LV_PART_MAIN);

  g_heating_track = lv_obj_create(parent);
  lv_obj_remove_style_all(g_heating_track);
  lv_obj_clear_flag(g_heating_track, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_width(g_heating_track, lv_pct(100));
  lv_obj_set_style_bg_color(g_heating_track, lv_color_hex(THERMOSTAT_COLOR_HEAT), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(g_heating_track, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(g_heating_track, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(g_heating_track, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(g_heating_track, 0, LV_PART_MAIN);

  thermostat_update_track_geometry();
}

void thermostat_create_setpoint_group(lv_obj_t *parent)
{
  g_setpoint_group = lv_obj_create(parent);
  lv_obj_remove_style_all(g_setpoint_group);
  lv_obj_clear_flag(g_setpoint_group, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(g_setpoint_group, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  lv_obj_set_width(g_setpoint_group, lv_pct(100));
  lv_obj_set_height(g_setpoint_group, LV_SIZE_CONTENT);
  lv_obj_set_pos(g_setpoint_group, 0, g_view_model.setpoint_group_y);
  lv_obj_set_style_pad_left(g_setpoint_group, 38, LV_PART_MAIN);
  lv_obj_set_style_pad_right(g_setpoint_group, 38, LV_PART_MAIN);
  lv_obj_set_layout(g_setpoint_group, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(g_setpoint_group, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(g_setpoint_group, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);

  g_cooling_container = lv_obj_create(g_setpoint_group);
  lv_obj_remove_style_all(g_cooling_container);
  lv_obj_clear_flag(g_cooling_container, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(g_cooling_container, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  lv_obj_set_layout(g_cooling_container, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(g_cooling_container, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(g_cooling_container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
  lv_obj_set_style_pad_column(g_cooling_container, 0, LV_PART_MAIN);
  lv_obj_set_width(g_cooling_container, 260);
  lv_obj_set_height(g_cooling_container, LV_SIZE_CONTENT);

  g_cooling_label = lv_label_create(g_cooling_container);
  lv_obj_set_style_text_font(g_cooling_label, g_fonts.setpoint_primary, LV_PART_MAIN);
  lv_obj_set_style_text_color(g_cooling_label, lv_color_hex(THERMOSTAT_COLOR_NEUTRAL), LV_PART_MAIN);
  lv_obj_set_size(g_cooling_label, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_label_set_long_mode(g_cooling_label, LV_LABEL_LONG_CLIP);

  g_cooling_fraction_label = lv_label_create(g_cooling_container);
  lv_obj_set_style_text_font(g_cooling_fraction_label, g_fonts.setpoint_secondary, LV_PART_MAIN);
  lv_obj_set_style_text_color(g_cooling_fraction_label, lv_color_hex(THERMOSTAT_COLOR_NEUTRAL), LV_PART_MAIN);
  lv_obj_set_size(g_cooling_fraction_label, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_translate_x(g_cooling_fraction_label, -30, LV_PART_MAIN);
  lv_obj_set_style_translate_y(g_cooling_fraction_label, -2, LV_PART_MAIN);
  lv_label_set_long_mode(g_cooling_fraction_label, LV_LABEL_LONG_CLIP);

  g_heating_container = lv_obj_create(g_setpoint_group);
  lv_obj_remove_style_all(g_heating_container);
  lv_obj_clear_flag(g_heating_container, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(g_heating_container, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  lv_obj_set_layout(g_heating_container, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(g_heating_container, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(g_heating_container, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
  lv_obj_set_style_pad_column(g_heating_container, 0, LV_PART_MAIN);
  lv_obj_set_width(g_heating_container, 260);
  lv_obj_set_height(g_heating_container, LV_SIZE_CONTENT);

  g_heating_label = lv_label_create(g_heating_container);
  lv_obj_set_style_text_font(g_heating_label, g_fonts.setpoint_primary, LV_PART_MAIN);
  lv_obj_set_style_text_color(g_heating_label, lv_color_hex(THERMOSTAT_COLOR_HEAT), LV_PART_MAIN);
  lv_obj_set_size(g_heating_label, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_translate_x(g_heating_label, 29, LV_PART_MAIN);
  lv_label_set_long_mode(g_heating_label, LV_LABEL_LONG_CLIP);

  g_heating_fraction_label = lv_label_create(g_heating_container);
  lv_obj_set_style_text_font(g_heating_fraction_label, g_fonts.setpoint_secondary, LV_PART_MAIN);
  lv_obj_set_style_text_color(g_heating_fraction_label, lv_color_hex(THERMOSTAT_COLOR_HEAT), LV_PART_MAIN);
  lv_obj_set_size(g_heating_fraction_label, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_translate_y(g_heating_fraction_label, -2, LV_PART_MAIN);
  lv_label_set_long_mode(g_heating_fraction_label, LV_LABEL_LONG_CLIP);

  thermostat_update_setpoint_labels();
  thermostat_update_active_setpoint_styles();
  thermostat_update_track_geometry();
  thermostat_position_setpoint_labels();
}

void thermostat_update_setpoint_labels(void)
{
  if (g_cooling_label == NULL || g_heating_label == NULL)
  {
    return;
  }

  char whole_buf[16];
  char fraction_buf[8];

  thermostat_format_setpoint(g_view_model.cooling_setpoint_c, whole_buf, sizeof(whole_buf), fraction_buf, sizeof(fraction_buf));
  lv_label_set_text(g_cooling_label, whole_buf);
  lv_label_set_text(g_cooling_fraction_label, fraction_buf);

  thermostat_format_setpoint(g_view_model.heating_setpoint_c, whole_buf, sizeof(whole_buf), fraction_buf, sizeof(fraction_buf));
  lv_label_set_text(g_heating_label, whole_buf);
  lv_label_set_text(g_heating_fraction_label, fraction_buf);

  thermostat_update_active_setpoint_styles();
}

void thermostat_update_active_setpoint_styles(void)
{
  const bool cooling_active = g_view_model.active_target == THERMOSTAT_TARGET_COOL;
  const bool heating_active = g_view_model.active_target == THERMOSTAT_TARGET_HEAT;
  const lv_opa_t cooling_label_opa = cooling_active ? THERMOSTAT_OPA_LABEL_ACTIVE : THERMOSTAT_OPA_LABEL_INACTIVE_COOL;
  const lv_opa_t heating_label_opa = heating_active ? THERMOSTAT_OPA_LABEL_ACTIVE : THERMOSTAT_OPA_LABEL_INACTIVE_HEAT;
  const lv_color_t color_cool = lv_color_hex(THERMOSTAT_COLOR_COOL);
  const lv_color_t color_heat = lv_color_hex(THERMOSTAT_COLOR_HEAT);
  const lv_color_t color_neutral = lv_color_hex(THERMOSTAT_COLOR_NEUTRAL);
  if (g_cooling_container)
  {
    lv_obj_set_style_opa(g_cooling_container, cooling_label_opa, LV_PART_MAIN);
  }
  if (g_heating_container)
  {
    lv_obj_set_style_opa(g_heating_container, heating_label_opa, LV_PART_MAIN);
  }

  if (g_cooling_label && g_cooling_fraction_label)
  {
    const lv_color_t color = cooling_active ? color_cool : color_neutral;
    lv_obj_set_style_text_color(g_cooling_label, color, LV_PART_MAIN);
    lv_obj_set_style_text_color(g_cooling_fraction_label, color, LV_PART_MAIN);
    lv_obj_set_style_opa(g_cooling_label, cooling_label_opa, LV_PART_MAIN);
    lv_obj_set_style_opa(g_cooling_fraction_label, cooling_label_opa, LV_PART_MAIN);
  }

  if (g_heating_label && g_heating_fraction_label)
  {
    lv_obj_set_style_text_color(g_heating_label, color_heat, LV_PART_MAIN);
    lv_obj_set_style_text_color(g_heating_fraction_label, color_heat, LV_PART_MAIN);
    lv_obj_set_style_opa(g_heating_label, heating_label_opa, LV_PART_MAIN);
    lv_obj_set_style_opa(g_heating_fraction_label, heating_label_opa, LV_PART_MAIN);
  }

  if (g_cooling_track)
  {
    lv_obj_set_style_bg_color(g_cooling_track, color_cool, LV_PART_MAIN);
    lv_obj_set_style_opa(g_cooling_track,
                         cooling_active ? LV_OPA_COVER : THERMOSTAT_OPA_TRACK_INACTIVE_COOL,
                         LV_PART_MAIN);
  }
  if (g_heating_track)
  {
    lv_obj_set_style_bg_color(g_heating_track, color_heat, LV_PART_MAIN);
    lv_obj_set_style_opa(g_heating_track,
                         heating_active ? LV_OPA_COVER : THERMOSTAT_OPA_TRACK_INACTIVE_HEAT,
                         LV_PART_MAIN);
  }

  thermostat_update_action_bar_visuals();
}

void thermostat_format_setpoint(float value, char *whole_buf, size_t whole_buf_sz,
                                char *fraction_buf, size_t fraction_buf_sz)
{
  const int tenths = (int)lroundf(value * 10.0f);
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

static const char *thermostat_target_name_local(thermostat_target_t target)
{
  return (target == THERMOSTAT_TARGET_COOL) ? "COOL" : "HEAT";
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
