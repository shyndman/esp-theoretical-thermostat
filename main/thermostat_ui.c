/**
 * @file thermostat_ui.c
 *
 */

/*********************
 *      INCLUDES
 *********************/
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <math.h>
#include "esp_log.h"
#include "lvgl.h"
#include "thermostat_ui.h"
#include "thermostat/ui_state.h"
#include "thermostat/ui_theme.h"
#include "thermostat/ui_top_bar.h"
#include "thermostat/ui_setpoint_view.h"
#include "thermostat/ui_setpoint_input.h"
#include "thermostat/ui_actions.h"
#include "thermostat/backlight_manager.h"
#include "thermostat/remote_setpoint_controller.h"

LV_IMG_DECLARE(room_default);

/*********************
 *      DEFINES
 *********************/

static void thermostat_ui_init(void);
static void thermostat_vm_init(void);
static lv_obj_t *thermostat_create_root_screen(void);
static void thermostat_root_input_event(lv_event_t *e);

thermostat_view_model_t g_view_model;
thermostat_font_bundle_t g_fonts;
lv_obj_t *g_root_screen = NULL;
lv_obj_t *g_layer_top = NULL;
bool g_ui_initialized = false;
static const char *TAG_UI = "thermostat_ui";

void thermostat_ui_attach(void)
{
  if (g_ui_initialized)
  {
    if (g_root_screen)
    {
      lv_scr_load(g_root_screen);
    }
    return;
  }

  thermostat_ui_init();
}

static void thermostat_vm_init(void)
{
  g_view_model.current_temp_c = THERMOSTAT_DEFAULT_ROOM_TEMP_C;
  g_view_model.cooling_setpoint_c = THERMOSTAT_DEFAULT_COOL_SETPOINT_C;
  g_view_model.heating_setpoint_c = THERMOSTAT_DEFAULT_HEAT_SETPOINT_C;
  g_view_model.active_target = THERMOSTAT_TARGET_HEAT;
  g_view_model.drag_active = false;
  g_view_model.weather_temp_c = 0.0f;
  g_view_model.weather_icon = NULL;
  g_view_model.weather_temp_valid = false;
  g_view_model.hvac_heating_active = false;
  g_view_model.hvac_cooling_active = false;
  g_view_model.hvac_status_error = false;
  g_view_model.room_temp_c = 0.0f;
  g_view_model.room_icon = &room_default;
  g_view_model.room_temp_valid = false;
  g_view_model.room_icon_error = false;
  g_view_model.weather_ready = false;
  g_view_model.room_ready = false;
  g_view_model.hvac_ready = false;
  g_view_model.system_powered = true;
  g_view_model.fan_running = false;
  g_view_model.fan_payload_error = false;
  g_view_model.cooling_setpoint_valid = true;
  g_view_model.heating_setpoint_valid = true;
  thermostat_slider_state_t cooling_state;
  thermostat_slider_state_t heating_state;
  thermostat_compute_state_from_temperature(g_view_model.cooling_setpoint_c, &cooling_state);
  thermostat_compute_state_from_temperature(g_view_model.heating_setpoint_c, &heating_state);
  thermostat_apply_state_to_target(THERMOSTAT_TARGET_COOL, &cooling_state);
  thermostat_apply_state_to_target(THERMOSTAT_TARGET_HEAT, &heating_state);
  g_view_model.setpoint_group_y = LV_MIN(g_view_model.cooling_label_y, g_view_model.heating_label_y);
  g_view_model.track_y_position = heating_state.track_y;
  g_view_model.slider_track_height = heating_state.track_height;
  g_view_model.last_touch_y = heating_state.track_y;
}

static lv_obj_t *thermostat_create_root_screen(void)
{
  lv_obj_t *scr = lv_obj_create(NULL);
  lv_obj_remove_style_all(scr);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_style(scr, &g_style_root, LV_PART_MAIN);
  lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_OFF);
  lv_obj_add_event_cb(scr, thermostat_root_input_event, LV_EVENT_PRESSED, NULL);
  lv_obj_add_event_cb(scr, thermostat_root_input_event, LV_EVENT_PRESSING, NULL);
  lv_obj_add_event_cb(scr, thermostat_root_input_event, LV_EVENT_GESTURE, NULL);
  return scr;
}

static void thermostat_ui_init(void)
{
  thermostat_vm_init();

  if (!thermostat_fonts_init())
  {
    fprintf(stderr, "[thermostat] Font initialization failed; falling back to default LVGL font\n");
  }

  thermostat_theme_init();
  g_root_screen = thermostat_create_root_screen();
  lv_scr_load(g_root_screen);
  g_layer_top = lv_layer_top();

  lv_obj_t *top_bar = thermostat_create_top_bar(g_root_screen);
  thermostat_create_weather_group(top_bar);
  thermostat_create_hvac_status_group(top_bar);
  thermostat_create_room_group(top_bar);
  thermostat_create_tracks(g_root_screen);
  thermostat_create_setpoint_group(g_layer_top);
  thermostat_create_setpoint_overlay(g_layer_top);
  thermostat_create_action_bar(g_root_screen);
  thermostat_remote_setpoint_controller_init();

  g_ui_initialized = true;
}

static void thermostat_root_input_event(lv_event_t *e)
{
  lv_event_code_t code = lv_event_get_code(e);
  switch (code)
  {
  case LV_EVENT_PRESSED:
  case LV_EVENT_PRESSING:
  case LV_EVENT_GESTURE:
    ESP_LOGI(TAG_UI, "Root interaction event=%d", code);
    bool consumed = backlight_manager_notify_interaction(BACKLIGHT_WAKE_REASON_TOUCH);
    if (consumed) {
      break;
    }
    break;
  default:
    ESP_LOGW(TAG_UI, "Unhandled root LVGL event=%d", code);
    break;
  }
}
