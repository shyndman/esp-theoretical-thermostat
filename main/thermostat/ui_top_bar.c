#include <stdlib.h>
#include "thermostat/ui_top_bar.h"
#include "thermostat/ui_state.h"
#include "thermostat/ui_theme.h"

static lv_obj_t *g_top_bar = NULL;
static lv_obj_t *g_weather_group = NULL;
static lv_obj_t *g_weather_icon = NULL;
static lv_obj_t *g_weather_temp_label = NULL;
static lv_obj_t *g_hvac_status_group = NULL;
static lv_obj_t *g_hvac_status_label = NULL;
static lv_obj_t *g_room_group = NULL;
static lv_obj_t *g_room_temp_label = NULL;
static lv_obj_t *g_room_icon = NULL;

void thermostat_update_weather_data(void);
void thermostat_update_room_data(void);
void thermostat_update_hvac_data(void);

lv_obj_t *thermostat_create_top_bar(lv_obj_t *parent)
{
  lv_obj_t *top_bar = lv_obj_create(parent);
  lv_obj_remove_style_all(top_bar);
  lv_obj_add_style(top_bar, &g_style_top_bar, LV_PART_MAIN);
  lv_obj_set_size(top_bar, lv_pct(100), 64);
  lv_obj_set_style_pad_top(top_bar, 12, LV_PART_MAIN);
  lv_obj_set_style_pad_left(top_bar, 20, LV_PART_MAIN);
  lv_obj_set_style_pad_right(top_bar, 20, LV_PART_MAIN);
  lv_obj_set_style_pad_bottom(top_bar, 0, LV_PART_MAIN);
  lv_obj_clear_flag(top_bar, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scrollbar_mode(top_bar, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_layout(top_bar, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(top_bar, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(top_bar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(top_bar, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_column(top_bar, 0, LV_PART_MAIN);
  g_top_bar = top_bar;
  return top_bar;
}

void thermostat_create_weather_group(lv_obj_t *parent)
{
  g_weather_group = lv_obj_create(parent);
  lv_obj_remove_style_all(g_weather_group);
  lv_obj_clear_flag(g_weather_group, LV_OBJ_FLAG_SCROLLABLE);
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

  char buffer[32];
  lv_snprintf(buffer, sizeof(buffer), "%.0f%s", g_view_model.weather_temp_c, THERMOSTAT_SYMBOL_DEG);
  lv_label_set_text(g_weather_temp_label, buffer);
  LV_LOG_INFO("Weather temp text: %s", buffer);
}

void thermostat_create_hvac_status_group(lv_obj_t *parent)
{
  g_hvac_status_group = lv_obj_create(parent);
  lv_obj_remove_style_all(g_hvac_status_group);
  lv_obj_clear_flag(g_hvac_status_group, LV_OBJ_FLAG_SCROLLABLE);
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

  if (g_view_model.hvac_heating_active)
  {
    lv_label_set_text(g_hvac_status_label, "HEATING");
    lv_obj_set_style_text_color(g_hvac_status_label, lv_color_hex(0xe1752e), LV_PART_MAIN);
    lv_obj_set_style_opa(g_hvac_status_label, LV_OPA_COVER, LV_PART_MAIN);
    return;
  }

  if (g_view_model.hvac_cooling_active)
  {
    lv_label_set_text(g_hvac_status_label, "COOLING");
    lv_obj_set_style_text_color(g_hvac_status_label, lv_color_hex(0x2776cc), LV_PART_MAIN);
    lv_obj_set_style_opa(g_hvac_status_label, LV_OPA_COVER, LV_PART_MAIN);
    return;
  }

  lv_label_set_text(g_hvac_status_label, "");
  lv_obj_set_style_opa(g_hvac_status_label, LV_OPA_TRANSP, LV_PART_MAIN);
}

void thermostat_create_room_group(lv_obj_t *parent)
{
  g_room_group = lv_obj_create(parent);
  lv_obj_remove_style_all(g_room_group);
  lv_obj_clear_flag(g_room_group, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_layout(g_room_group, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(g_room_group, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(g_room_group, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(g_room_group, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_column(g_room_group, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_left(g_room_group, 10, LV_PART_MAIN);
  lv_obj_set_width(g_room_group, 240);

  g_room_temp_label = lv_label_create(g_room_group);
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

  char buffer[32];
  lv_snprintf(buffer, sizeof(buffer), "%.0f%s", g_view_model.room_temp_c, THERMOSTAT_SYMBOL_DEG);
  lv_label_set_text(g_room_temp_label, buffer);
  LV_LOG_INFO("Room temp text: %s", buffer);
}

void thermostat_schedule_top_bar_updates(void)
{
  lv_timer_create(thermostat_weather_timer_cb, 700, NULL);
  lv_timer_create(thermostat_room_timer_cb, 900, NULL);
  lv_timer_create(thermostat_hvac_timer_cb, 1200, NULL);
}

void thermostat_weather_timer_cb(lv_timer_t *timer)
{
  LV_UNUSED(timer);
  thermostat_update_weather_data();
}

void thermostat_room_timer_cb(lv_timer_t *timer)
{
  LV_UNUSED(timer);
  thermostat_update_room_data();
}

void thermostat_hvac_timer_cb(lv_timer_t *timer)
{
  LV_UNUSED(timer);
  thermostat_update_hvac_data();
}

void thermostat_update_weather_data(void)
{
  g_view_model.weather_temp_c = 5.0f + ((float)(rand() % 260) / 10.0f);
  g_view_model.weather_ready = true;
  thermostat_update_weather_group();
  thermostat_fade_in_widget(g_weather_icon);
  thermostat_fade_in_widget(g_weather_temp_label);
}

void thermostat_update_hvac_data(void)
{
  g_view_model.hvac_heating_active = (rand() % 2) == 0;
  g_view_model.hvac_cooling_active = !g_view_model.hvac_heating_active && (rand() % 2) == 0;
  g_view_model.hvac_ready = true;
  thermostat_update_hvac_status_group();
  thermostat_fade_in_widget(g_hvac_status_label);
}

void thermostat_update_room_data(void)
{
  g_view_model.room_temp_c = 18.0f + ((float)(rand() % 80) / 10.0f);
  g_view_model.room_ready = true;
  thermostat_update_room_group();
  thermostat_fade_in_widget(g_room_temp_label);
  thermostat_fade_in_widget(g_room_icon);
}
