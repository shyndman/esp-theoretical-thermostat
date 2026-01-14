#include "thermostat/ui_top_bar.h"
#include "thermostat/ui_animation_timing.h"
#include "thermostat/ui_state.h"
#include "thermostat/ui_theme.h"
#include "thermostat/ui_helpers.h"

static lv_obj_t *g_weather_group = NULL;
static lv_obj_t *g_weather_icon = NULL;
static lv_obj_t *g_weather_temp_label = NULL;
static lv_obj_t *g_hvac_status_group = NULL;
static lv_obj_t *g_hvac_status_label = NULL;
static lv_obj_t *g_room_group = NULL;
static lv_obj_t *g_room_temp_label = NULL;
static lv_obj_t *g_room_icon = NULL;

static void hvac_pulse_exec_cb(void *var, int32_t value);
static void hvac_start_pulse(lv_obj_t *label);
static void hvac_stop_pulse(lv_obj_t *label);

static void hvac_pulse_exec_cb(void *var, int32_t value)
{
  lv_obj_t *label = (lv_obj_t *)var;
  lv_obj_set_style_opa(label, value, LV_PART_MAIN);
}

static void hvac_start_pulse(lv_obj_t *label)
{
  if (label == NULL)
  {
    return;
  }

  lv_anim_del(label, hvac_pulse_exec_cb);
  lv_obj_set_style_opa(label, THERMOSTAT_OPA_HVAC_PULSE_MIN, LV_PART_MAIN);

  lv_anim_t anim;
  lv_anim_init(&anim);
  lv_anim_set_var(&anim, label);
  lv_anim_set_exec_cb(&anim, hvac_pulse_exec_cb);
  lv_anim_set_values(&anim, THERMOSTAT_OPA_HVAC_PULSE_MIN, LV_OPA_COVER);
  lv_anim_set_time(&anim, THERMOSTAT_ANIM_HVAC_PULSE_MS / 2);
  lv_anim_set_playback_time(&anim, THERMOSTAT_ANIM_HVAC_PULSE_MS / 2);
  lv_anim_set_path_cb(&anim, lv_anim_path_ease_in_out);
  lv_anim_set_repeat_count(&anim, LV_ANIM_REPEAT_INFINITE);
  lv_anim_start(&anim);
}

static void hvac_stop_pulse(lv_obj_t *label)
{
  if (label == NULL)
  {
    return;
  }

  lv_anim_del(label, hvac_pulse_exec_cb);
}

lv_obj_t *thermostat_create_top_bar(lv_obj_t *parent)
{
  lv_obj_t *top_bar = lv_obj_create(parent);
  thermostat_ui_reset_container(top_bar);
  lv_obj_add_style(top_bar, &g_style_top_bar, LV_PART_MAIN);
  lv_obj_set_size(top_bar, lv_pct(100), 64);
  lv_obj_set_style_pad_top(top_bar, 12, LV_PART_MAIN);
  lv_obj_set_style_pad_left(top_bar, 20, LV_PART_MAIN);
  lv_obj_set_style_pad_right(top_bar, 20, LV_PART_MAIN);
  lv_obj_set_style_pad_bottom(top_bar, 0, LV_PART_MAIN);
  lv_obj_set_layout(top_bar, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(top_bar, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(top_bar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(top_bar, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_column(top_bar, 0, LV_PART_MAIN);
  return top_bar;
}

void thermostat_create_weather_group(lv_obj_t *parent)
{
  g_weather_group = lv_obj_create(parent);
  thermostat_ui_reset_container(g_weather_group);
  lv_obj_set_layout(g_weather_group, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(g_weather_group, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(g_weather_group, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(g_weather_group, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_column(g_weather_group, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_right(g_weather_group, 12, LV_PART_MAIN);
  lv_obj_set_width(g_weather_group, 240);

  g_weather_icon = lv_img_create(g_weather_group);
  lv_obj_set_style_pad_right(g_weather_icon, 4, LV_PART_MAIN);
  lv_obj_set_style_img_recolor(g_weather_icon, lv_color_hex(0xa0a0a0), LV_PART_MAIN);
  lv_obj_set_style_img_recolor_opa(g_weather_icon, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_opa(g_weather_icon, LV_OPA_TRANSP, LV_PART_MAIN);
  if (g_view_model.weather_icon != NULL)
  {
    lv_img_set_src(g_weather_icon, g_view_model.weather_icon);
  }

  g_weather_temp_label = lv_label_create(g_weather_group);
  lv_obj_set_style_text_color(g_weather_temp_label, lv_color_hex(0xa0a0a0), LV_PART_MAIN);
  // Tabular figures keep the digits from shifting position every time the value updates.
  lv_obj_set_style_text_font(g_weather_temp_label, g_fonts.top_bar_large, LV_PART_MAIN);
  lv_obj_set_style_pad_left(g_weather_temp_label, 8, LV_PART_MAIN);
  lv_obj_set_width(g_weather_temp_label, LV_SIZE_CONTENT);
  lv_label_set_long_mode(g_weather_temp_label, LV_LABEL_LONG_CLIP);
  lv_obj_set_style_opa(g_weather_temp_label, LV_OPA_TRANSP, LV_PART_MAIN);

  thermostat_update_weather_group();
}

void thermostat_update_weather_group(void)
{
  if (g_weather_temp_label == NULL)
  {
    return;
  }

  if (!g_view_model.weather_ready)
  {
    lv_obj_set_style_opa(g_weather_temp_label, LV_OPA_TRANSP, LV_PART_MAIN);
    if (g_weather_icon)
    {
      lv_obj_set_style_opa(g_weather_icon, LV_OPA_TRANSP, LV_PART_MAIN);
    }
    return;
  }

  if (g_view_model.weather_temp_valid)
  {
    char buffer[32];
    lv_snprintf(buffer, sizeof(buffer), "%.1f%s", g_view_model.weather_temp_c, THERMOSTAT_SYMBOL_DEG);
    lv_label_set_text(g_weather_temp_label, buffer);
    lv_obj_set_style_text_color(g_weather_temp_label, lv_color_hex(0xa0a0a0), LV_PART_MAIN);
  }
  else
  {
    lv_label_set_text(g_weather_temp_label, "ERR");
    lv_obj_set_style_text_color(g_weather_temp_label, lv_color_hex(THERMOSTAT_ERROR_COLOR_HEX), LV_PART_MAIN);
  }
  lv_obj_set_style_opa(g_weather_temp_label, LV_OPA_COVER, LV_PART_MAIN);

  if (g_weather_icon)
  {
    if (g_view_model.weather_icon != NULL)
    {
      lv_img_set_src(g_weather_icon, g_view_model.weather_icon);
      lv_obj_set_style_opa(g_weather_icon, LV_OPA_COVER, LV_PART_MAIN);
    }
    else
    {
      lv_obj_set_style_opa(g_weather_icon, LV_OPA_TRANSP, LV_PART_MAIN);
    }
  }
}

void thermostat_create_hvac_status_group(lv_obj_t *parent)
{
  g_hvac_status_group = lv_obj_create(parent);
  thermostat_ui_reset_container(g_hvac_status_group);
  lv_obj_set_layout(g_hvac_status_group, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(g_hvac_status_group, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(g_hvac_status_group, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_width(g_hvac_status_group, 240);

  g_hvac_status_label = lv_label_create(g_hvac_status_group);
  lv_obj_set_style_text_font(g_hvac_status_label, g_fonts.top_bar_status, LV_PART_MAIN);
  lv_obj_set_style_text_letter_space(g_hvac_status_label, 2, LV_PART_MAIN);
  lv_label_set_long_mode(g_hvac_status_label, LV_LABEL_LONG_CLIP);
  lv_obj_set_style_text_align(g_hvac_status_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_set_style_opa(g_hvac_status_label, LV_OPA_TRANSP, LV_PART_MAIN);

  thermostat_update_hvac_status_group();
}

void thermostat_update_hvac_status_group(void)
{
  if (g_hvac_status_label == NULL)
  {
    return;
  }

  if (!g_view_model.hvac_ready)
  {
    hvac_stop_pulse(g_hvac_status_label);
    lv_obj_set_style_opa(g_hvac_status_label, LV_OPA_TRANSP, LV_PART_MAIN);
    return;
  }

  if (g_view_model.hvac_status_error)
  {
    hvac_stop_pulse(g_hvac_status_label);
    lv_label_set_text(g_hvac_status_label, "ERROR");
    lv_obj_set_style_text_color(g_hvac_status_label, lv_color_hex(THERMOSTAT_ERROR_COLOR_HEX), LV_PART_MAIN);
    lv_obj_set_style_opa(g_hvac_status_label, LV_OPA_COVER, LV_PART_MAIN);
    return;
  }

  if (g_view_model.hvac_heating_active)
  {
    lv_label_set_text(g_hvac_status_label, "HEATING");
    lv_obj_set_style_text_color(g_hvac_status_label, lv_color_hex(0xe1752e), LV_PART_MAIN);
    hvac_start_pulse(g_hvac_status_label);
    return;
  }

  if (g_view_model.hvac_cooling_active)
  {
    lv_label_set_text(g_hvac_status_label, "COOLING");
    lv_obj_set_style_text_color(g_hvac_status_label, lv_color_hex(0x2776cc), LV_PART_MAIN);
    hvac_start_pulse(g_hvac_status_label);
    return;
  }

  hvac_stop_pulse(g_hvac_status_label);
  lv_label_set_text(g_hvac_status_label, "");
  lv_obj_set_style_opa(g_hvac_status_label, LV_OPA_TRANSP, LV_PART_MAIN);
}

void thermostat_create_room_group(lv_obj_t *parent)
{
  g_room_group = lv_obj_create(parent);
  thermostat_ui_reset_container(g_room_group);
  lv_obj_set_layout(g_room_group, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(g_room_group, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(g_room_group, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(g_room_group, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_column(g_room_group, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_left(g_room_group, 10, LV_PART_MAIN);
  lv_obj_set_width(g_room_group, 240);

  g_room_temp_label = lv_label_create(g_room_group);
  // Matching tabular numerals keep the room temperature steady on screen as digits change.
  lv_obj_set_style_text_font(g_room_temp_label, g_fonts.top_bar_large, LV_PART_MAIN);
  lv_obj_set_style_text_color(g_room_temp_label, lv_color_hex(0xa0a0a0), LV_PART_MAIN);
  lv_obj_set_style_pad_right(g_room_temp_label, 8, LV_PART_MAIN);
  lv_obj_set_width(g_room_temp_label, LV_SIZE_CONTENT);
  lv_label_set_long_mode(g_room_temp_label, LV_LABEL_LONG_CLIP);
  lv_obj_set_style_opa(g_room_temp_label, LV_OPA_TRANSP, LV_PART_MAIN);

  g_room_icon = lv_img_create(g_room_group);
  lv_obj_set_style_img_recolor(g_room_icon, lv_color_hex(0xa0a0a0), LV_PART_MAIN);
  lv_obj_set_style_img_recolor_opa(g_room_icon, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_pad_left(g_room_icon, 8, LV_PART_MAIN);
  lv_obj_set_style_opa(g_room_icon, LV_OPA_TRANSP, LV_PART_MAIN);
  if (g_view_model.room_icon != NULL)
  {
    lv_img_set_src(g_room_icon, g_view_model.room_icon);
  }

  thermostat_update_room_group();
}

void thermostat_update_room_group(void)
{
  if (g_room_temp_label == NULL)
  {
    return;
  }

  if (!g_view_model.room_ready)
  {
    lv_obj_set_style_opa(g_room_temp_label, LV_OPA_TRANSP, LV_PART_MAIN);
    if (g_room_icon)
    {
      lv_obj_set_style_opa(g_room_icon, LV_OPA_TRANSP, LV_PART_MAIN);
    }
    return;
  }

  if (g_view_model.room_temp_valid)
  {
    char buffer[32];
    lv_snprintf(buffer, sizeof(buffer), "%.1f%s", g_view_model.room_temp_c, THERMOSTAT_SYMBOL_DEG);
    lv_label_set_text(g_room_temp_label, buffer);
    lv_obj_set_style_text_color(g_room_temp_label, lv_color_hex(0xa0a0a0), LV_PART_MAIN);
  }
  else
  {
    lv_label_set_text(g_room_temp_label, "ERR");
    lv_obj_set_style_text_color(g_room_temp_label, lv_color_hex(THERMOSTAT_ERROR_COLOR_HEX), LV_PART_MAIN);
  }
  lv_obj_set_style_opa(g_room_temp_label, LV_OPA_COVER, LV_PART_MAIN);

  if (g_room_icon)
  {
    if (g_view_model.room_icon)
    {
      lv_img_set_src(g_room_icon, g_view_model.room_icon);
    }
    lv_color_t color = g_view_model.room_icon_error ? lv_color_hex(THERMOSTAT_ERROR_COLOR_HEX) : lv_color_hex(0xa0a0a0);
    lv_obj_set_style_img_recolor(g_room_icon, color, LV_PART_MAIN);
    lv_obj_set_style_img_recolor_opa(g_room_icon, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_opa(g_room_icon, LV_OPA_COVER, LV_PART_MAIN);
  }
}

lv_obj_t *thermostat_get_weather_group(void)
{
  return g_weather_group;
}

lv_obj_t *thermostat_get_hvac_status_group(void)
{
  return g_hvac_status_group;
}

lv_obj_t *thermostat_get_room_group(void)
{
  return g_room_group;
}
