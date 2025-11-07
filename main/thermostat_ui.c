/**
 * @file thermostat_ui.c
 *
 */

/*********************
 *      INCLUDES
 *********************/
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <time.h>
#include <stdint.h>
#include <math.h>
#include "lvgl.h"
#include "thermostat_ui.h"
#include "thermostat/ui_state.h"
#include "thermostat/ui_theme.h"
#include "thermostat/ui_top_bar.h"
#include "thermostat/ui_setpoint_view.h"
#include "thermostat/ui_setpoint_input.h"
#include "thermostat/ui_actions.h"

LV_IMG_DECLARE(sunny);
LV_IMG_DECLARE(room_default);

/*********************
 *      DEFINES
 *********************/

static void thermostat_ui_init(void);
static void thermostat_vm_init(void);
static lv_obj_t *thermostat_create_root_screen(void);

thermostat_view_model_t g_view_model;
thermostat_font_bundle_t g_fonts;
lv_obj_t *g_root_screen = NULL;
lv_obj_t *g_layer_top = NULL;
float g_layout_scale = 1.0f;
bool g_ui_initialized = false;

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
  static bool rng_seeded = false;
  if (!rng_seeded)
  {
    srand((unsigned)time(NULL));
    rng_seeded = true;
  }

  g_view_model.current_temp_c = THERMOSTAT_DEFAULT_ROOM_TEMP_C;
  g_view_model.cooling_setpoint_c = THERMOSTAT_DEFAULT_COOL_SETPOINT_C;
  g_view_model.heating_setpoint_c = THERMOSTAT_DEFAULT_HEAT_SETPOINT_C;
  g_view_model.active_target = THERMOSTAT_TARGET_HEAT;
  g_view_model.drag_active = false;
  g_view_model.pending_drag_active = false;
  g_view_model.weather_temp_c = 5.0f + ((float)(rand() % 200) / 10.0f); /* 5.0°C to 25.0°C */
  g_view_model.weather_icon = &sunny;
  g_view_model.hvac_heating_active = (rand() % 2) == 0;
  g_view_model.hvac_cooling_active = !g_view_model.hvac_heating_active && (rand() % 2) == 0;
  g_view_model.room_temp_c = 19.0f + ((float)(rand() % 60) / 10.0f); /* 19.0°C to 25.0°C */
  g_view_model.room_icon = &room_default;
  g_view_model.weather_ready = false;
  g_view_model.room_ready = false;
  g_view_model.hvac_ready = false;
  g_view_model.system_powered = true;
  g_view_model.fan_running = false;
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
  g_view_model.pending_drag_start_y = 0;
}

static lv_obj_t *thermostat_create_root_screen(void)
{
  lv_obj_t *scr = lv_obj_create(NULL);
  lv_obj_remove_style_all(scr);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_style(scr, &g_style_root, LV_PART_MAIN);
  lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_OFF);
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
  lv_disp_t *disp = lv_disp_get_default();
  if (disp)
  {
    g_layout_scale = (float)lv_disp_get_ver_res(disp) / THERMOSTAT_TRACK_PANEL_HEIGHT;
    if (g_layout_scale <= 0.0f)
    {
      g_layout_scale = 1.0f;
    }
  }
  else
  {
    g_layout_scale = 1.0f;
  }
  g_layer_top = lv_layer_top();

  lv_obj_t *top_bar = thermostat_create_top_bar(g_root_screen);
  thermostat_create_weather_group(top_bar);
  thermostat_create_hvac_status_group(top_bar);
  thermostat_create_room_group(top_bar);
  thermostat_schedule_top_bar_updates();
  thermostat_create_tracks(g_root_screen);
  thermostat_create_setpoint_group(g_layer_top);
  thermostat_create_touch_zone(g_root_screen);
  thermostat_create_action_bar(g_root_screen);

  g_ui_initialized = true;
}
