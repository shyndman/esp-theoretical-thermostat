#pragma once

#include <stdbool.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

#define THERMOSTAT_ERROR_COLOR_HEX 0xff4d4f

extern lv_style_t g_style_root;
extern lv_style_t g_style_top_bar;

bool thermostat_fonts_init(void);
void thermostat_theme_init(void);
void thermostat_fade_in_widget(lv_obj_t *obj);

#ifdef __cplusplus
}
#endif
