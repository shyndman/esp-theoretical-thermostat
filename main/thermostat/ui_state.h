#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  THERMOSTAT_TARGET_COOL = 0,
  THERMOSTAT_TARGET_HEAT = 1,
} thermostat_target_t;

typedef struct {
  float current_temp_c;
  float cooling_setpoint_c;
  float heating_setpoint_c;
  thermostat_target_t active_target;
  bool drag_active;
  float weather_temp_c;
  const lv_img_dsc_t *weather_icon;
  bool weather_temp_valid;
  bool hvac_heating_active;
  bool hvac_cooling_active;
  bool hvac_status_error;
  float room_temp_c;
  const lv_img_dsc_t *room_icon;
  bool room_temp_valid;
  bool room_icon_error;
  bool weather_ready;
  bool room_ready;
  bool hvac_ready;
  bool fan_running;
  bool fan_payload_error;
  bool cooling_setpoint_valid;
  bool heating_setpoint_valid;
  int track_y_position;
  int slider_track_height;
  int last_touch_y;
  int cooling_label_y;
  int heating_label_y;
  int cooling_track_y;
  int cooling_track_height;
  int heating_track_y;
  int heating_track_height;
  int setpoint_group_y;
} thermostat_view_model_t;

typedef struct {
  const lv_font_t *setpoint_primary;
  const lv_font_t *setpoint_secondary;
  const lv_font_t *top_bar_medium;
  const lv_font_t *top_bar_large;
  const lv_font_t *top_bar_status;
} thermostat_font_bundle_t;

#define THERMOSTAT_DEFAULT_ROOM_TEMP_C (21.0f)
#define THERMOSTAT_DEFAULT_COOL_SETPOINT_C (24.0f)
#define THERMOSTAT_DEFAULT_HEAT_SETPOINT_C (21.0f)

#define THERMOSTAT_SYMBOL_DEG "\xC2\xB0"

/*
 * >>> READ THIS BEFORE TOUCHING ANY COLORS <<<
 * The five macros below are the only blessed palette entries for the thermostat UI.
 * Do not change their values, add new colors, or remix them unless design explicitly OKs it.
 * The inactive variants are 50% desaturated versions of cool/heat for inactive state styling.
 * Treat this palette as sacred.
 */
#define THERMOSTAT_COLOR_COOL 0x2776cc
#define THERMOSTAT_COLOR_HEAT 0xe1752e
#define THERMOSTAT_COLOR_NEUTRAL 0x292929
#define THERMOSTAT_COLOR_COOL_INACTIVE 0x5393c9
#define THERMOSTAT_COLOR_HEAT_INACTIVE 0x4a4a4a

#define THERMOSTAT_OPA_LABEL_ACTIVE LV_OPA_COVER
#define THERMOSTAT_OPA_LABEL_INACTIVE_COOL ((LV_OPA_COVER * 40) / 100)
#define THERMOSTAT_OPA_LABEL_INACTIVE_HEAT ((LV_OPA_COVER * 65) / 100)
#define THERMOSTAT_OPA_TRACK_INACTIVE_COOL ((LV_OPA_COVER * 40) / 100)
#define THERMOSTAT_OPA_TRACK_INACTIVE_HEAT ((LV_OPA_COVER * 65) / 100)



#define THERMOSTAT_MIN_TEMP_C 14.0f
#define THERMOSTAT_MAX_TEMP_C 28.0f
#define THERMOSTAT_TEMP_STEP_C 0.1f
#define THERMOSTAT_IDEAL_TEMP_C 21.0f
#define THERMOSTAT_HEAT_OVERRUN_C 0.3f
#define THERMOSTAT_COOL_OVERRUN_C 0.3f
#define THERMOSTAT_TRACK_TOP_Y 320.0f
#define THERMOSTAT_IDEAL_LABEL_Y 680.0f
#define THERMOSTAT_TRACK_PANEL_HEIGHT 1280.0f
#define THERMOSTAT_LABEL_OFFSET 86.0f

extern thermostat_view_model_t g_view_model;
extern thermostat_font_bundle_t g_fonts;
extern lv_obj_t *g_root_screen;
extern lv_obj_t *g_layer_top;
extern bool g_ui_initialized;

#ifdef __cplusplus
}
#endif
