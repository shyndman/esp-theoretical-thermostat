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
  bool hvac_heating_active;
  bool hvac_cooling_active;
  float room_temp_c;
  const lv_img_dsc_t *room_icon;
  bool weather_ready;
  bool room_ready;
  bool hvac_ready;
  bool system_powered;
  bool fan_running;
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
 * The three macros below are the only blessed palette entries for the thermostat UI.
 * Do not change their values, add new colors, or remix them unless design explicitly OKs it.
 * All state differentiation (active vs inactive, emphasis, etc.) must be achieved via opacity
 * or downstream style logic using these canonical hues. Treat this palette as sacred.
 */
#define THERMOSTAT_COLOR_COOL 0x2776cc
#define THERMOSTAT_COLOR_HEAT 0xe1752e
#define THERMOSTAT_COLOR_NEUTRAL 0x292929

#define THERMOSTAT_OPA_LABEL_ACTIVE LV_OPA_COVER
#define THERMOSTAT_OPA_LABEL_INACTIVE_COOL LV_OPA_COVER
#define THERMOSTAT_OPA_LABEL_INACTIVE_HEAT ((LV_OPA_COVER * 70) / 100)
#define THERMOSTAT_OPA_TRACK_INACTIVE_COOL LV_OPA_COVER
#define THERMOSTAT_OPA_TRACK_INACTIVE_HEAT ((LV_OPA_COVER * 70) / 100)

#ifndef LV_MIN
#define LV_MIN(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef LV_MAX
#define LV_MAX(a, b) ((a) > (b) ? (a) : (b))
#endif

#define THERMOSTAT_MIN_TEMP_C 16.0f
#define THERMOSTAT_MAX_TEMP_C 30.0f
#define THERMOSTAT_TEMP_STEP_C 0.2f
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
