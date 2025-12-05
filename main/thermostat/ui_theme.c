#include "thermostat/ui_theme.h"
#include "thermostat/ui_state.h"
#include "assets/fonts/thermostat_fonts.h"

lv_style_t g_style_root;
lv_style_t g_style_top_bar;
lv_style_t g_style_tick_whole;
lv_style_t g_style_tick_half;
static bool g_theme_initialized = false;

// Tick opacity (dimension constants are in ui_theme.h)
#define THERMOSTAT_TICK_OPA ((LV_OPA_COVER * 18) / 100)

static void thermostat_fade_exec_cb(void *var, int32_t value);

bool thermostat_fonts_init(void)
{
  g_fonts.setpoint_primary = THERMOSTAT_FONT_SETPOINT_PRIMARY;
  g_fonts.setpoint_secondary = THERMOSTAT_FONT_SETPOINT_SECONDARY;
  g_fonts.top_bar_large = THERMOSTAT_FONT_TOP_BAR_LARGE;
  g_fonts.top_bar_medium = THERMOSTAT_FONT_TOP_BAR_MEDIUM;
  g_fonts.top_bar_status = THERMOSTAT_FONT_TOP_BAR_STATUS;
  return g_fonts.setpoint_primary && g_fonts.setpoint_secondary && g_fonts.top_bar_large && g_fonts.top_bar_medium &&
         g_fonts.top_bar_status;
}

void thermostat_theme_init(void)
{
  if (g_theme_initialized)
  {
    return;
  }

  lv_style_init(&g_style_root);
  lv_style_set_bg_color(&g_style_root, lv_color_black());
  lv_style_set_bg_opa(&g_style_root, LV_OPA_COVER);
  lv_style_set_pad_all(&g_style_root, 0);
  lv_style_set_pad_row(&g_style_root, 0);
  lv_style_set_pad_column(&g_style_root, 0);

  lv_style_init(&g_style_top_bar);
  lv_style_set_bg_opa(&g_style_top_bar, LV_OPA_TRANSP);
  lv_style_set_border_width(&g_style_top_bar, 0);
  lv_style_set_outline_width(&g_style_top_bar, 0);

  // Whole-degree tick style: 12px wide, 2px stroke, semi-transparent white
  lv_style_init(&g_style_tick_whole);
  lv_style_set_line_color(&g_style_tick_whole, lv_color_white());
  lv_style_set_line_opa(&g_style_tick_whole, THERMOSTAT_TICK_OPA);
  lv_style_set_line_width(&g_style_tick_whole, THERMOSTAT_TICK_WHOLE_STROKE);
  lv_style_set_line_rounded(&g_style_tick_whole, false);

  // Half-degree tick style: 7px wide, 1px stroke, semi-transparent white
  lv_style_init(&g_style_tick_half);
  lv_style_set_line_color(&g_style_tick_half, lv_color_white());
  lv_style_set_line_opa(&g_style_tick_half, THERMOSTAT_TICK_OPA);
  lv_style_set_line_width(&g_style_tick_half, THERMOSTAT_TICK_HALF_STROKE);
  lv_style_set_line_rounded(&g_style_tick_half, false);

  g_theme_initialized = true;
}

void thermostat_fade_in_widget(lv_obj_t *obj)
{
  if (obj == NULL)
    return;
  lv_anim_t anim;
  lv_anim_init(&anim);
  lv_anim_set_var(&anim, obj);
  lv_anim_set_time(&anim, 250);
  lv_anim_set_values(&anim, lv_obj_get_style_opa(obj, LV_PART_MAIN), LV_OPA_COVER);
  lv_anim_set_exec_cb(&anim, thermostat_fade_exec_cb);
  lv_anim_start(&anim);
}

static void thermostat_fade_exec_cb(void *var, int32_t value)
{
  lv_obj_set_style_opa((lv_obj_t *)var, (lv_opa_t)value, LV_PART_MAIN);
}
