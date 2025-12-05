#pragma once

#include <stdbool.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

#define THERMOSTAT_ERROR_COLOR_HEX 0xff4d4f

// Tick overlay dimensions (design spec: padding-width column)
#define THERMOSTAT_TICK_OVERLAY_WIDTH  12
#define THERMOSTAT_TICK_WHOLE_WIDTH    12
#define THERMOSTAT_TICK_HALF_WIDTH     7
#define THERMOSTAT_TICK_WHOLE_STROKE   12
#define THERMOSTAT_TICK_HALF_STROKE    8

extern lv_style_t g_style_root;
extern lv_style_t g_style_top_bar;
extern lv_style_t g_style_tick_whole;
extern lv_style_t g_style_tick_half;

bool thermostat_fonts_init(void);
void thermostat_theme_init(void);
void thermostat_fade_in_widget(lv_obj_t *obj);

#ifdef __cplusplus
}
#endif
