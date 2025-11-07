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
#if LV_USE_FREETYPE
  #include "lvgl/src/libs/freetype/lv_freetype.h"
#endif
#include "thermostat_ui.h"

LV_IMG_DECLARE(sunny);
LV_IMG_DECLARE(room_default);
LV_IMG_DECLARE(power);
LV_IMG_DECLARE(snowflake);
LV_IMG_DECLARE(fire);
LV_IMG_DECLARE(fan);

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/
typedef enum {
  THERMOSTAT_TARGET_COOL = 0,
  THERMOSTAT_TARGET_HEAT = 1,
} thermostat_target_t;

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void thermostat_ui_init(void);
static void thermostat_vm_init(void);
static bool thermostat_fonts_init(void);
static void thermostat_theme_init(void);
static lv_obj_t *thermostat_create_root_screen(void);
static lv_obj_t *thermostat_create_top_bar(lv_obj_t *parent);
static void thermostat_create_weather_group(lv_obj_t *parent);
static void thermostat_update_weather_group(void);
static void thermostat_create_hvac_status_group(lv_obj_t *parent);
static void thermostat_update_hvac_status_group(void);
static void thermostat_create_room_group(lv_obj_t *parent);
static void thermostat_update_room_group(void);
static void thermostat_create_tracks(lv_obj_t *parent);
static void thermostat_update_track_geometry(void);
static void thermostat_create_touch_zone(lv_obj_t *parent);
static void thermostat_track_touch_event(lv_event_t *e);
static void thermostat_create_action_bar(lv_obj_t *parent);
static void thermostat_update_action_bar_visuals(void);
static void thermostat_create_setpoint_group(lv_obj_t *parent);
static void thermostat_update_setpoint_labels(void);
static void thermostat_update_active_setpoint_styles(void);
static void thermostat_handle_setpoint_event(lv_event_t *e);
static void thermostat_format_setpoint(float value, char *whole_buf, size_t whole_buf_sz,
                                       char *fraction_buf, size_t fraction_buf_sz);
static void thermostat_position_setpoint_labels(void);
static void thermostat_select_setpoint_target(thermostat_target_t target);
static void thermostat_commit_setpoints(void);
static void thermostat_handle_drag_sample(int sample_y);
static void thermostat_update_layer_order(void);
static void thermostat_select_target_near(int sample_y);
static void thermostat_schedule_top_bar_updates(void);
static void thermostat_update_weather_data(void);
static void thermostat_update_room_data(void);
static void thermostat_update_hvac_data(void);
static void thermostat_fade_in_widget(lv_obj_t *obj);
static void thermostat_fade_exec_cb(void *var, int32_t value);
static void thermostat_mode_icon_event(lv_event_t *e);
static void thermostat_power_icon_event(lv_event_t *e);
static void thermostat_fan_icon_event(lv_event_t *e);
static void thermostat_fan_spin_exec_cb(void *obj, int32_t value);
static lv_coord_t thermostat_scale_coord(int base_value);
static lv_coord_t thermostat_scale_length(int base_value);
static int thermostat_to_base_y(int screen_y);


/**********************
 *  STATIC VARIABLES
 **********************/
typedef struct {
  float current_temp_c;
  float cooling_setpoint_c;
  float heating_setpoint_c;
  thermostat_target_t active_target;
  bool drag_active;
  bool pending_drag_active;
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
  int pending_drag_start_y;
  int cooling_label_y;
  int heating_label_y;
  int cooling_track_y;
  int cooling_track_height;
  int heating_track_y;
  int heating_track_height;
  int setpoint_group_y;
} thermostat_view_model_t;

typedef struct {
  lv_font_t *setpoint_primary;
  lv_font_t *setpoint_secondary;
  lv_font_t *top_bar_medium;
  lv_font_t *top_bar_large;
} thermostat_font_bundle_t;

static thermostat_view_model_t g_view_model;
static thermostat_font_bundle_t g_fonts;
static lv_obj_t *g_root_screen = NULL;
static lv_obj_t *g_layer_top = NULL;
static float g_layout_scale = 1.0f;
static lv_obj_t *g_top_bar = NULL;
static lv_obj_t *g_weather_group = NULL;
static lv_obj_t *g_weather_icon = NULL;
static lv_obj_t *g_weather_temp_label = NULL;
static lv_obj_t *g_hvac_status_group = NULL;
static lv_obj_t *g_hvac_status_label = NULL;
static lv_obj_t *g_room_group = NULL;
static lv_obj_t *g_room_temp_label = NULL;
static lv_obj_t *g_room_icon = NULL;
static lv_obj_t *g_setpoint_group = NULL;
static lv_obj_t *g_cooling_container = NULL;
static lv_obj_t *g_heating_container = NULL;
static lv_obj_t *g_cooling_label = NULL;
static lv_obj_t *g_cooling_fraction_label = NULL;
static lv_obj_t *g_heating_label = NULL;
static lv_obj_t *g_heating_fraction_label = NULL;
static lv_obj_t *g_cooling_track = NULL;
static lv_obj_t *g_heating_track = NULL;
static lv_obj_t *g_track_touch_zone = NULL;
static lv_obj_t *g_action_bar = NULL;
static lv_obj_t *g_mode_icon = NULL;
static lv_obj_t *g_power_icon = NULL;
static lv_obj_t *g_fan_icon = NULL;

static lv_style_t g_style_root;
static lv_style_t g_style_top_bar;
static bool g_theme_initialized = false;
static bool g_ui_initialized = false;

void thermostat_ui_attach(void)
{
  if(g_ui_initialized) {
    if(g_root_screen) {
      lv_scr_load(g_root_screen);
    }
    return;
  }

  thermostat_ui_init();
}

/**********************
 *      MACROS
 **********************/
#define THERMOSTAT_DEFAULT_ROOM_TEMP_C (21.0f)
#define THERMOSTAT_DEFAULT_COOL_SETPOINT_C (24.0f)
#define THERMOSTAT_DEFAULT_HEAT_SETPOINT_C (21.0f)

#define THERMOSTAT_FONT_PATH_PRIMARY "assets/fonts/Figtree-tnum-SemiBold.otf"
#define THERMOSTAT_FONT_PATH_SECONDARY "assets/fonts/Figtree-tnum-Medium.otf"
#define THERMOSTAT_FONT_PATH_TOP_BAR "assets/fonts/Figtree-Medium.otf"
#define THERMOSTAT_FONT_SIZE_SETPOINT_PRIMARY 120
#define THERMOSTAT_FONT_SIZE_SETPOINT_SECONDARY 50
#define THERMOSTAT_FONT_SIZE_TOP_BAR_LARGE 39
#define THERMOSTAT_FONT_SIZE_TOP_BAR_MEDIUM 34
#define THERMOSTAT_SYMBOL_DEG "\xC2\xB0"
#define THERMOSTAT_COLOR_COOL_TEXT 0x292929
#define THERMOSTAT_COLOR_HEAT_TEXT 0xe1752e
#define THERMOSTAT_COLOR_COOL_ACTIVE 0x2776cc
#define THERMOSTAT_COLOR_HEAT_ACTIVE 0xe1752e
#define THERMOSTAT_COLOR_COOL_INACTIVE 0x4a4a4a
#define THERMOSTAT_COLOR_HEAT_INACTIVE 0x5b3a2f
#define THERMOSTAT_COLOR_TRACK_INACTIVE_COOL 0x303030
#define THERMOSTAT_COLOR_TRACK_INACTIVE_HEAT 0x3a2a2a
#define THERMOSTAT_OPA_TRACK_INACTIVE_COOL LV_OPA_COVER
#define THERMOSTAT_OPA_TRACK_INACTIVE_HEAT LV_OPA_COVER
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
#define THERMOSTAT_LABEL_OFFSET 90.0f

typedef struct {
  int track_y;
  int track_height;
  int label_y;
  float setpoint;
} thermostat_slider_state_t;

static const float k_slider_slope = (THERMOSTAT_IDEAL_LABEL_Y - THERMOSTAT_TRACK_TOP_Y) /
                                    (THERMOSTAT_IDEAL_TEMP_C - THERMOSTAT_MAX_TEMP_C);
static const float k_slider_intercept = THERMOSTAT_TRACK_TOP_Y - (k_slider_slope * THERMOSTAT_MAX_TEMP_C);
static const int k_track_min_y = (int)(THERMOSTAT_TRACK_TOP_Y + 0.5f);
static const int k_track_max_y = (int)((k_slider_slope * THERMOSTAT_MIN_TEMP_C + k_slider_intercept) + 0.5f);

/**********************
 *   STATIC FUNCTIONS
 **********************/
#if LV_USE_FREETYPE
static lv_font_t *thermostat_load_font(const char *path, uint32_t size)
{
  lv_font_t *font = lv_freetype_font_create(path, LV_FREETYPE_FONT_RENDER_MODE_BITMAP, size,
                                            LV_FREETYPE_FONT_STYLE_NORMAL);
  if(font == NULL) {
    fprintf(stderr, "[thermostat] Failed to load font '%s' (size=%u)\n", path, size);
  }
  return font;
}
#endif

static float thermostat_clamp_temperature(float value)
{
  if(value < THERMOSTAT_MIN_TEMP_C) return THERMOSTAT_MIN_TEMP_C;
  if(value > THERMOSTAT_MAX_TEMP_C) return THERMOSTAT_MAX_TEMP_C;
  return value;
}

static float thermostat_round_to_step(float value)
{
  return roundf(value / THERMOSTAT_TEMP_STEP_C) * THERMOSTAT_TEMP_STEP_C;
}

static int thermostat_clamp_track_y(int y)
{
  if(y < k_track_min_y) return k_track_min_y;
  if(y > k_track_max_y) return k_track_max_y;
  return y;
}

static float thermostat_temperature_from_y(int track_y)
{
  float raw = (track_y - k_slider_intercept) / k_slider_slope;
  return thermostat_clamp_temperature(thermostat_round_to_step(raw));
}

static int thermostat_track_y_from_temperature(float temp)
{
  float clamped = thermostat_clamp_temperature(temp);
  float raw = (k_slider_slope * clamped) + k_slider_intercept;
  int y = (int)roundf(raw);
  return thermostat_clamp_track_y(y);
}

static int thermostat_compute_label_y(int track_y)
{
  int label_y = (int)lrintf(track_y - THERMOSTAT_LABEL_OFFSET);
  if(label_y < 120) label_y = 120;
  return label_y;
}

static int thermostat_compute_track_height(int track_y)
{
  int height = (int)(THERMOSTAT_TRACK_PANEL_HEIGHT - track_y);
  if(height < 0) height = 0;
  return height;
}

static void thermostat_compute_state_from_temperature(float temp, thermostat_slider_state_t *state)
{
  state->setpoint = thermostat_clamp_temperature(thermostat_round_to_step(temp));
  state->track_y = thermostat_track_y_from_temperature(state->setpoint);
  state->track_height = thermostat_compute_track_height(state->track_y);
  state->label_y = thermostat_compute_label_y(state->track_y);
}

static void thermostat_compute_state_from_y(int sample_y, thermostat_slider_state_t *state)
{
  int clamped_y = thermostat_clamp_track_y(sample_y);
  float temp = thermostat_temperature_from_y(clamped_y);
  thermostat_compute_state_from_temperature(temp, state);
}

static float thermostat_clamp_cooling(float candidate, float heating_setpoint)
{
  float min_gap = THERMOSTAT_TEMP_STEP_C + THERMOSTAT_HEAT_OVERRUN_C;
  float min_limit = heating_setpoint + min_gap;
  min_limit = ceilf(min_limit / THERMOSTAT_TEMP_STEP_C) * THERMOSTAT_TEMP_STEP_C;
  if(min_limit > THERMOSTAT_MAX_TEMP_C) min_limit = THERMOSTAT_MAX_TEMP_C;
  float rounded = thermostat_round_to_step(candidate);
  if(rounded < min_limit) rounded = min_limit;
  return thermostat_clamp_temperature(rounded);
}

static float thermostat_clamp_heating(float candidate, float cooling_setpoint)
{
  float limit = cooling_setpoint - (THERMOSTAT_TEMP_STEP_C + THERMOSTAT_COOL_OVERRUN_C);
  float stepped_limit = floorf(limit / THERMOSTAT_TEMP_STEP_C) * THERMOSTAT_TEMP_STEP_C;
  if(stepped_limit < THERMOSTAT_MIN_TEMP_C) stepped_limit = THERMOSTAT_MIN_TEMP_C;
  float rounded = thermostat_round_to_step(candidate);
  if(rounded > cooling_setpoint) rounded = cooling_setpoint;
  if(rounded > stepped_limit) rounded = stepped_limit;
  return thermostat_clamp_temperature(rounded);
}

static void thermostat_apply_state_to_target(thermostat_target_t target, const thermostat_slider_state_t *state)
{
  if(target == THERMOSTAT_TARGET_COOL) {
    g_view_model.cooling_setpoint_c = state->setpoint;
    g_view_model.cooling_track_y = state->track_y;
    g_view_model.cooling_track_height = state->track_height;
    g_view_model.cooling_label_y = state->label_y;
  } else {
    g_view_model.heating_setpoint_c = state->setpoint;
    g_view_model.heating_track_y = state->track_y;
    g_view_model.heating_track_height = state->track_height;
    g_view_model.heating_label_y = state->label_y;
  }
}

static void thermostat_sync_active_slider_state(const thermostat_slider_state_t *state)
{
  g_view_model.track_y_position = state->track_y;
  g_view_model.slider_track_height = state->track_height;
  g_view_model.last_touch_y = state->track_y;
}

static void thermostat_vm_init(void)
{
  static bool rng_seeded = false;
  if(!rng_seeded) {
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

static bool thermostat_fonts_init(void)
{
#if LV_USE_FREETYPE
  g_fonts.setpoint_primary = thermostat_load_font(THERMOSTAT_FONT_PATH_PRIMARY, THERMOSTAT_FONT_SIZE_SETPOINT_PRIMARY);
  g_fonts.setpoint_secondary = thermostat_load_font(THERMOSTAT_FONT_PATH_SECONDARY,
                                                    THERMOSTAT_FONT_SIZE_SETPOINT_SECONDARY);
  g_fonts.top_bar_large = thermostat_load_font(THERMOSTAT_FONT_PATH_TOP_BAR, THERMOSTAT_FONT_SIZE_TOP_BAR_LARGE);
  g_fonts.top_bar_medium = thermostat_load_font(THERMOSTAT_FONT_PATH_TOP_BAR, THERMOSTAT_FONT_SIZE_TOP_BAR_MEDIUM);

  return g_fonts.setpoint_primary && g_fonts.setpoint_secondary && g_fonts.top_bar_large && g_fonts.top_bar_medium;
#else
  g_fonts.setpoint_primary = (lv_font_t *)LV_FONT_DEFAULT;
  g_fonts.setpoint_secondary = (lv_font_t *)LV_FONT_DEFAULT;
  g_fonts.top_bar_medium = (lv_font_t *)LV_FONT_DEFAULT;
  g_fonts.top_bar_large = (lv_font_t *)LV_FONT_DEFAULT;
  fprintf(stderr,
          "[thermostat] LV_USE_FREETYPE is disabled; build with FreeType to use tabular Figtree fonts\n");
  return false;
#endif
}

static void thermostat_theme_init(void)
{
  if(g_theme_initialized) {
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

  g_theme_initialized = true;
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

static lv_obj_t *thermostat_create_top_bar(lv_obj_t *parent)
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
  return top_bar;
}

static void thermostat_create_weather_group(lv_obj_t *parent)
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
  if(g_view_model.weather_icon != NULL) {
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

static void thermostat_update_weather_group(void)
{
  if(g_weather_temp_label == NULL) {
    return;
  }

  char buffer[32];
  lv_snprintf(buffer, sizeof(buffer), "%.0f%s", g_view_model.weather_temp_c, THERMOSTAT_SYMBOL_DEG);
  lv_label_set_text(g_weather_temp_label, buffer);
  LV_LOG_INFO("Weather temp text: %s", buffer);
}

static void thermostat_create_room_group(lv_obj_t *parent)
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
  if(g_view_model.room_icon != NULL) {
    lv_img_set_src(g_room_icon, g_view_model.room_icon);
  }

  thermostat_update_room_group();
}

static void thermostat_update_room_group(void)
{
  if(g_room_temp_label == NULL) {
    return;
  }

  char buffer[32];
  lv_snprintf(buffer, sizeof(buffer), "%.0f%s", g_view_model.room_temp_c, THERMOSTAT_SYMBOL_DEG);
  lv_label_set_text(g_room_temp_label, buffer);
  LV_LOG_INFO("Room temp text: %s", buffer);
}

static void thermostat_create_tracks(lv_obj_t *parent)
{
  g_cooling_track = lv_obj_create(parent);
  lv_obj_remove_style_all(g_cooling_track);
  lv_obj_clear_flag(g_cooling_track, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_width(g_cooling_track, lv_pct(100));
  lv_obj_set_style_bg_color(g_cooling_track, lv_color_hex(0x2776cc), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(g_cooling_track, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(g_cooling_track, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(g_cooling_track, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(g_cooling_track, 0, LV_PART_MAIN);

  g_heating_track = lv_obj_create(parent);
  lv_obj_remove_style_all(g_heating_track);
  lv_obj_clear_flag(g_heating_track, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_width(g_heating_track, lv_pct(100));
  lv_obj_set_style_bg_color(g_heating_track, lv_color_hex(0xe1752e), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(g_heating_track, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(g_heating_track, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(g_heating_track, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(g_heating_track, 0, LV_PART_MAIN);

  thermostat_update_track_geometry();
  thermostat_update_layer_order();
}

static void thermostat_update_track_geometry(void)
{
  if(g_cooling_track) {
    lv_obj_set_y(g_cooling_track, thermostat_scale_coord(g_view_model.cooling_track_y));
    lv_obj_set_height(g_cooling_track, thermostat_scale_length(g_view_model.cooling_track_height));
  }
  if(g_heating_track) {
    lv_obj_set_y(g_heating_track, thermostat_scale_coord(g_view_model.heating_track_y));
    lv_obj_set_height(g_heating_track, thermostat_scale_length(g_view_model.heating_track_height));
  }

  if(g_cooling_track && g_heating_track) {
    lv_obj_move_to_index(g_cooling_track, 0);
    lv_obj_move_to_index(g_heating_track, 1);
  }
  if(g_track_touch_zone) {
    lv_obj_move_to_index(g_track_touch_zone, 2);
  }
  thermostat_update_layer_order();
}

static void thermostat_update_layer_order(void)
{
  if(g_cooling_track) {
    lv_obj_move_to_index(g_cooling_track, 0);
  }
  if(g_heating_track) {
    lv_obj_move_to_index(g_heating_track, 1);
  }
  if(g_track_touch_zone) {
    lv_obj_move_to_index(g_track_touch_zone, 2);
  }
  if(g_action_bar) {
    lv_obj_move_foreground(g_action_bar);
  }
  if(g_setpoint_group && g_layer_top) {
    lv_obj_move_foreground(g_setpoint_group);
  }
}

static void thermostat_create_touch_zone(lv_obj_t *parent)
{
  g_track_touch_zone = lv_obj_create(parent);
  lv_obj_remove_style_all(g_track_touch_zone);
  lv_obj_clear_flag(g_track_touch_zone, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(g_track_touch_zone, lv_pct(100), thermostat_scale_length(1160));
  lv_obj_set_pos(g_track_touch_zone, 0, thermostat_scale_coord(120));
  lv_obj_set_style_bg_opa(g_track_touch_zone, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_add_flag(g_track_touch_zone, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(g_track_touch_zone, thermostat_track_touch_event, LV_EVENT_PRESSED, NULL);
  lv_obj_add_event_cb(g_track_touch_zone, thermostat_track_touch_event, LV_EVENT_PRESSING, NULL);
  lv_obj_add_event_cb(g_track_touch_zone, thermostat_track_touch_event, LV_EVENT_RELEASED, NULL);
  lv_obj_add_event_cb(g_track_touch_zone, thermostat_track_touch_event, LV_EVENT_PRESS_LOST, NULL);

  thermostat_update_track_geometry();
}

static void thermostat_create_action_bar(lv_obj_t *parent)
{
  g_action_bar = lv_obj_create(parent);
  lv_obj_remove_style_all(g_action_bar);
  lv_obj_clear_flag(g_action_bar, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_width(g_action_bar, lv_pct(100));
  lv_obj_set_height(g_action_bar, LV_SIZE_CONTENT);
  lv_obj_set_style_pad_left(g_action_bar, 80, LV_PART_MAIN);
  lv_obj_set_style_pad_right(g_action_bar, 80, LV_PART_MAIN);
  lv_obj_set_style_pad_row(g_action_bar, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_column(g_action_bar, 40, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(g_action_bar, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_layout(g_action_bar, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(g_action_bar, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(g_action_bar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_pos(g_action_bar, 0, thermostat_scale_coord(1120));

  g_mode_icon = lv_img_create(g_action_bar);
  lv_obj_add_event_cb(g_mode_icon, thermostat_mode_icon_event, LV_EVENT_CLICKED, NULL);
  lv_obj_set_style_img_recolor(g_mode_icon, lv_color_hex(0xdadada), LV_PART_MAIN);
  lv_obj_set_style_img_recolor_opa(g_mode_icon, LV_OPA_COVER, LV_PART_MAIN);

  g_power_icon = lv_img_create(g_action_bar);
  lv_img_set_src(g_power_icon, &power);
  lv_obj_add_event_cb(g_power_icon, thermostat_power_icon_event, LV_EVENT_CLICKED, NULL);
  lv_obj_set_style_img_recolor(g_power_icon, lv_color_hex(0xdadada), LV_PART_MAIN);
  lv_obj_set_style_img_recolor_opa(g_power_icon, LV_OPA_COVER, LV_PART_MAIN);

  g_fan_icon = lv_img_create(g_action_bar);
  lv_img_set_src(g_fan_icon, &fan);
  lv_img_set_pivot(g_fan_icon, lv_obj_get_width(g_fan_icon) / 2, lv_obj_get_height(g_fan_icon) / 2);
  lv_obj_add_event_cb(g_fan_icon, thermostat_fan_icon_event, LV_EVENT_CLICKED, NULL);
  lv_obj_set_style_img_recolor(g_fan_icon, lv_color_hex(0xdadada), LV_PART_MAIN);
  lv_obj_set_style_img_recolor_opa(g_fan_icon, LV_OPA_COVER, LV_PART_MAIN);

  thermostat_update_action_bar_visuals();
}

static void thermostat_track_touch_event(lv_event_t *e)
{
  lv_event_code_t code = lv_event_get_code(e);
  lv_indev_t *indev = lv_event_get_indev(e);
  if(indev == NULL) {
    return;
  }
  lv_point_t point;
  lv_indev_get_point(indev, &point);

  if(code == LV_EVENT_PRESSED) {
    g_view_model.drag_active = false;
    g_view_model.pending_drag_active = true;
    g_view_model.pending_drag_start_y = point.y;
    thermostat_select_target_near(point.y);
  } else if(code == LV_EVENT_PRESSING) {
    if(g_view_model.pending_drag_active) {
      const int delta = LV_ABS(point.y - g_view_model.pending_drag_start_y);
      if(delta > 8) {
        g_view_model.pending_drag_active = false;
        g_view_model.drag_active = true;
      }
    }
    if(g_view_model.drag_active) {
      thermostat_handle_drag_sample(point.y);
    }
  } else if(code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
    if(g_view_model.drag_active) {
      thermostat_handle_drag_sample(point.y);
    }
    g_view_model.drag_active = false;
    g_view_model.pending_drag_active = false;
    thermostat_commit_setpoints();
  }
}

static void thermostat_create_hvac_status_group(lv_obj_t *parent)
{
  g_hvac_status_group = lv_obj_create(parent);
  lv_obj_remove_style_all(g_hvac_status_group);
  lv_obj_clear_flag(g_hvac_status_group, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_layout(g_hvac_status_group, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(g_hvac_status_group, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(g_hvac_status_group, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_width(g_hvac_status_group, 240);

  g_hvac_status_label = lv_label_create(g_hvac_status_group);
  lv_obj_set_style_text_font(g_hvac_status_label, g_fonts.top_bar_medium, LV_PART_MAIN);
  lv_obj_set_style_text_letter_space(g_hvac_status_label, 2, LV_PART_MAIN);
  lv_label_set_long_mode(g_hvac_status_label, LV_LABEL_LONG_CLIP);
  lv_obj_set_style_text_align(g_hvac_status_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_set_style_opa(g_hvac_status_label, LV_OPA_TRANSP, LV_PART_MAIN);

  thermostat_update_hvac_status_group();
}

static void thermostat_update_hvac_status_group(void)
{
  if(g_hvac_status_label == NULL) {
    return;
  }

  if(g_view_model.hvac_heating_active) {
    lv_label_set_text(g_hvac_status_label, "HEATING");
    lv_obj_set_style_text_color(g_hvac_status_label, lv_color_hex(0xe1752e), LV_PART_MAIN);
    lv_obj_set_style_opa(g_hvac_status_label, LV_OPA_COVER, LV_PART_MAIN);
    return;
  }

  if(g_view_model.hvac_cooling_active) {
    lv_label_set_text(g_hvac_status_label, "COOLING");
    lv_obj_set_style_text_color(g_hvac_status_label, lv_color_hex(0x2776cc), LV_PART_MAIN);
    lv_obj_set_style_opa(g_hvac_status_label, LV_OPA_COVER, LV_PART_MAIN);
    return;
  }

  lv_label_set_text(g_hvac_status_label, "");
  lv_obj_set_style_opa(g_hvac_status_label, LV_OPA_TRANSP, LV_PART_MAIN);
}

static void thermostat_update_action_bar_visuals(void)
{
  if(g_mode_icon) {
    const lv_img_dsc_t *src = (g_view_model.active_target == THERMOSTAT_TARGET_COOL) ? &snowflake : &fire;
    lv_img_set_src(g_mode_icon, src);
    lv_obj_set_style_img_recolor(g_mode_icon, lv_color_hex(0xdadada), LV_PART_MAIN);
    lv_obj_set_style_img_recolor_opa(g_mode_icon, LV_OPA_COVER, LV_PART_MAIN);
  }

  if(g_power_icon) {
    lv_obj_set_style_img_recolor(g_power_icon,
                                 g_view_model.system_powered ? lv_color_hex(0xdadada) : lv_color_hex(0x505050),
                                 LV_PART_MAIN);
    lv_obj_set_style_img_recolor_opa(g_power_icon, LV_OPA_COVER, LV_PART_MAIN);
  }

  if(g_fan_icon) {
    lv_obj_set_style_img_recolor(g_fan_icon,
                                 g_view_model.fan_running ? lv_color_hex(0xdadada) : lv_color_hex(0x505050),
                                 LV_PART_MAIN);
    lv_obj_set_style_img_recolor_opa(g_fan_icon, LV_OPA_COVER, LV_PART_MAIN);
    if(g_view_model.fan_running) {
      lv_anim_del(g_fan_icon, NULL);
      lv_anim_t anim;
      lv_anim_init(&anim);
      lv_anim_set_var(&anim, g_fan_icon);
      lv_anim_set_exec_cb(&anim, thermostat_fan_spin_exec_cb);
      lv_anim_set_time(&anim, 1200);
      lv_anim_set_repeat_count(&anim, LV_ANIM_REPEAT_INFINITE);
      lv_anim_set_values(&anim, 0, 3600);
      lv_anim_start(&anim);
    } else {
      lv_anim_del(g_fan_icon, NULL);
      lv_img_set_angle(g_fan_icon, 0);
    }
  }
}

static void thermostat_format_setpoint(float value, char *whole_buf, size_t whole_buf_sz,
                                       char *fraction_buf, size_t fraction_buf_sz)
{
  const int tenths = (int)lroundf(value * 10.0f);
  const int whole = tenths / 10;
  int fraction = tenths % 10;
  if(fraction < 0) {
    fraction = -fraction;
  }

  lv_snprintf(whole_buf, whole_buf_sz, "%d%s", whole, THERMOSTAT_SYMBOL_DEG);
  lv_snprintf(fraction_buf, fraction_buf_sz, ".%d", fraction);
}

static void thermostat_position_setpoint_labels(void)
{
  if(g_setpoint_group == NULL || g_cooling_container == NULL || g_heating_container == NULL) {
    return;
  }

  int base = LV_MIN(g_view_model.cooling_label_y, g_view_model.heating_label_y);
  g_view_model.setpoint_group_y = base;
  lv_obj_set_y(g_setpoint_group, thermostat_scale_coord(base));
  lv_obj_set_style_translate_y(g_cooling_container,
                               thermostat_scale_length(g_view_model.cooling_label_y - base),
                               LV_PART_MAIN);
  lv_obj_set_style_translate_y(g_heating_container,
                               thermostat_scale_length(g_view_model.heating_label_y - base),
                               LV_PART_MAIN);
}

static void thermostat_create_setpoint_group(lv_obj_t *parent)
{
  g_setpoint_group = lv_obj_create(parent);
  lv_obj_remove_style_all(g_setpoint_group);
  lv_obj_clear_flag(g_setpoint_group, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(g_setpoint_group, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  lv_obj_set_width(g_setpoint_group, lv_pct(100));
  lv_obj_set_height(g_setpoint_group, LV_SIZE_CONTENT);
  lv_obj_set_pos(g_setpoint_group, 0, thermostat_scale_coord(g_view_model.setpoint_group_y));
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
  lv_obj_add_event_cb(g_cooling_container, thermostat_handle_setpoint_event, LV_EVENT_PRESSED,
                      (void *)THERMOSTAT_TARGET_COOL);

  g_cooling_label = lv_label_create(g_cooling_container);
  lv_obj_set_style_text_font(g_cooling_label, g_fonts.setpoint_primary, LV_PART_MAIN);
  lv_obj_set_style_text_color(g_cooling_label, lv_color_hex(THERMOSTAT_COLOR_COOL_TEXT), LV_PART_MAIN);
  lv_obj_set_size(g_cooling_label, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_label_set_long_mode(g_cooling_label, LV_LABEL_LONG_CLIP);
  lv_obj_add_event_cb(g_cooling_label, thermostat_handle_setpoint_event, LV_EVENT_PRESSED, (void *)THERMOSTAT_TARGET_COOL);
  lv_obj_add_event_cb(g_cooling_label, thermostat_handle_setpoint_event, LV_EVENT_RELEASED, (void *)THERMOSTAT_TARGET_COOL);

  g_cooling_fraction_label = lv_label_create(g_cooling_container);
  lv_obj_set_style_text_font(g_cooling_fraction_label, g_fonts.setpoint_secondary, LV_PART_MAIN);
  lv_obj_set_style_text_color(g_cooling_fraction_label, lv_color_hex(THERMOSTAT_COLOR_COOL_TEXT), LV_PART_MAIN);
  lv_obj_set_size(g_cooling_fraction_label, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_translate_x(g_cooling_fraction_label, -29, LV_PART_MAIN);
  lv_obj_set_style_translate_y(g_cooling_fraction_label, -13, LV_PART_MAIN);
  lv_obj_set_style_pad_bottom(g_cooling_fraction_label, 4, LV_PART_MAIN);
  lv_label_set_long_mode(g_cooling_fraction_label, LV_LABEL_LONG_CLIP);
  lv_obj_add_event_cb(g_cooling_fraction_label, thermostat_handle_setpoint_event, LV_EVENT_PRESSED,
                      (void *)THERMOSTAT_TARGET_COOL);
  lv_obj_add_event_cb(g_cooling_fraction_label, thermostat_handle_setpoint_event, LV_EVENT_RELEASED,
                      (void *)THERMOSTAT_TARGET_COOL);

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
  lv_obj_add_event_cb(g_heating_container, thermostat_handle_setpoint_event, LV_EVENT_PRESSED,
                      (void *)THERMOSTAT_TARGET_HEAT);

  g_heating_label = lv_label_create(g_heating_container);
  lv_obj_set_style_text_font(g_heating_label, g_fonts.setpoint_primary, LV_PART_MAIN);
  lv_obj_set_style_text_color(g_heating_label, lv_color_hex(THERMOSTAT_COLOR_HEAT_TEXT), LV_PART_MAIN);
  lv_obj_set_size(g_heating_label, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_translate_x(g_heating_label, 29, LV_PART_MAIN);
  lv_label_set_long_mode(g_heating_label, LV_LABEL_LONG_CLIP);
  lv_obj_add_event_cb(g_heating_label, thermostat_handle_setpoint_event, LV_EVENT_PRESSED, (void *)THERMOSTAT_TARGET_HEAT);
  lv_obj_add_event_cb(g_heating_label, thermostat_handle_setpoint_event, LV_EVENT_RELEASED, (void *)THERMOSTAT_TARGET_HEAT);

  g_heating_fraction_label = lv_label_create(g_heating_container);
  lv_obj_set_style_text_font(g_heating_fraction_label, g_fonts.setpoint_secondary, LV_PART_MAIN);
  lv_obj_set_style_text_color(g_heating_fraction_label, lv_color_hex(THERMOSTAT_COLOR_HEAT_TEXT), LV_PART_MAIN);
  lv_obj_set_size(g_heating_fraction_label, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_translate_y(g_heating_fraction_label, -13, LV_PART_MAIN);
  lv_obj_set_style_pad_bottom(g_heating_fraction_label, 4, LV_PART_MAIN);
  lv_label_set_long_mode(g_heating_fraction_label, LV_LABEL_LONG_CLIP);
  lv_obj_add_event_cb(g_heating_fraction_label, thermostat_handle_setpoint_event, LV_EVENT_PRESSED,
                      (void *)THERMOSTAT_TARGET_HEAT);
  lv_obj_add_event_cb(g_heating_fraction_label, thermostat_handle_setpoint_event, LV_EVENT_RELEASED,
                      (void *)THERMOSTAT_TARGET_HEAT);

  thermostat_update_setpoint_labels();
  thermostat_update_active_setpoint_styles();
  thermostat_update_track_geometry();
  thermostat_position_setpoint_labels();
}

static void thermostat_update_setpoint_labels(void)
{
  if(g_cooling_label == NULL || g_heating_label == NULL) {
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

static void thermostat_update_active_setpoint_styles(void)
{
  if(g_cooling_container == NULL || g_heating_container == NULL) {
    return;
  }

  lv_obj_set_style_opa(g_cooling_container, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_opa(g_heating_container, LV_OPA_COVER, LV_PART_MAIN);

  const bool cooling_active = g_view_model.active_target == THERMOSTAT_TARGET_COOL;
  const bool heating_active = g_view_model.active_target == THERMOSTAT_TARGET_HEAT;

  if(g_cooling_label && g_cooling_fraction_label) {
    const lv_color_t color = lv_color_hex(cooling_active ? THERMOSTAT_COLOR_COOL_ACTIVE : THERMOSTAT_COLOR_COOL_INACTIVE);
    lv_obj_set_style_text_color(g_cooling_label, color, LV_PART_MAIN);
    lv_obj_set_style_text_color(g_cooling_fraction_label, color, LV_PART_MAIN);
  }

  if(g_heating_label && g_heating_fraction_label) {
    const lv_color_t color = lv_color_hex(heating_active ? THERMOSTAT_COLOR_HEAT_ACTIVE : THERMOSTAT_COLOR_HEAT_INACTIVE);
    lv_obj_set_style_text_color(g_heating_label, color, LV_PART_MAIN);
    lv_obj_set_style_text_color(g_heating_fraction_label, color, LV_PART_MAIN);
  }

  if(g_cooling_track) {
    lv_obj_set_style_bg_color(g_cooling_track,
                              lv_color_hex(cooling_active ? THERMOSTAT_COLOR_COOL_ACTIVE : THERMOSTAT_COLOR_TRACK_INACTIVE_COOL),
                              LV_PART_MAIN);
    lv_obj_set_style_bg_opa(g_cooling_track,
                            cooling_active ? LV_OPA_COVER : THERMOSTAT_OPA_TRACK_INACTIVE_COOL,
                            LV_PART_MAIN);
  }
  if(g_heating_track) {
    lv_obj_set_style_bg_color(g_heating_track,
                              lv_color_hex(heating_active ? THERMOSTAT_COLOR_HEAT_ACTIVE : THERMOSTAT_COLOR_TRACK_INACTIVE_HEAT),
                              LV_PART_MAIN);
    lv_obj_set_style_bg_opa(g_heating_track,
                            heating_active ? LV_OPA_COVER : THERMOSTAT_OPA_TRACK_INACTIVE_HEAT,
                            LV_PART_MAIN);
  }

  thermostat_update_action_bar_visuals();
}

static void thermostat_handle_setpoint_event(lv_event_t *e)
{
  const thermostat_target_t target = (thermostat_target_t)(intptr_t)lv_event_get_user_data(e);
  switch(lv_event_get_code(e)) {
    case LV_EVENT_PRESSED:
      thermostat_select_setpoint_target(target);
      break;
    case LV_EVENT_RELEASED:
      thermostat_commit_setpoints();
      break;
    case LV_EVENT_LONG_PRESSED_REPEAT:
      thermostat_select_setpoint_target(target);
      break;
    default:
      break;
  }
}

static void thermostat_select_setpoint_target(thermostat_target_t target)
{
  g_view_model.active_target = target;
  const float temp = (target == THERMOSTAT_TARGET_COOL) ? g_view_model.cooling_setpoint_c
                                                       : g_view_model.heating_setpoint_c;
  thermostat_slider_state_t state;
  thermostat_compute_state_from_temperature(temp, &state);
  thermostat_apply_state_to_target(target, &state);
  thermostat_sync_active_slider_state(&state);
  thermostat_position_setpoint_labels();
  thermostat_update_track_geometry();
  thermostat_update_active_setpoint_styles();
}

static void thermostat_commit_setpoints(void)
{
  LV_LOG_INFO("Committing setpoints cooling=%.1f heating=%.1f",
              g_view_model.cooling_setpoint_c,
              g_view_model.heating_setpoint_c);
}

static void thermostat_handle_drag_sample(int sample_y)
{
  int base_sample = thermostat_to_base_y(sample_y);
  thermostat_slider_state_t state;
  thermostat_compute_state_from_y(base_sample, &state);

  if(g_view_model.active_target == THERMOSTAT_TARGET_COOL) {
    float clamped = thermostat_clamp_cooling(state.setpoint, g_view_model.heating_setpoint_c);
    thermostat_compute_state_from_temperature(clamped, &state);
    thermostat_apply_state_to_target(THERMOSTAT_TARGET_COOL, &state);
  } else {
    float clamped = thermostat_clamp_heating(state.setpoint, g_view_model.cooling_setpoint_c);
    thermostat_compute_state_from_temperature(clamped, &state);
    thermostat_apply_state_to_target(THERMOSTAT_TARGET_HEAT, &state);
  }

  thermostat_sync_active_slider_state(&state);
  thermostat_update_setpoint_labels();
  thermostat_position_setpoint_labels();
  thermostat_update_track_geometry();
}

static void thermostat_select_target_near(int sample_y)
{
  int base_sample = thermostat_to_base_y(sample_y);
  thermostat_slider_state_t cool_state;
  thermostat_slider_state_t heat_state;
  thermostat_compute_state_from_temperature(g_view_model.cooling_setpoint_c, &cool_state);
  thermostat_compute_state_from_temperature(g_view_model.heating_setpoint_c, &heat_state);

  int dist_cool = LV_ABS(base_sample - cool_state.label_y);
  int dist_heat = LV_ABS(base_sample - heat_state.label_y);
  thermostat_target_t desired = (dist_cool <= dist_heat) ? THERMOSTAT_TARGET_COOL : THERMOSTAT_TARGET_HEAT;

  if(desired != g_view_model.active_target) {
  thermostat_select_setpoint_target(desired);
  }
}

static void thermostat_weather_timer_cb(lv_timer_t *timer);
static void thermostat_room_timer_cb(lv_timer_t *timer);
static void thermostat_hvac_timer_cb(lv_timer_t *timer);

static void thermostat_schedule_top_bar_updates(void)
{
  lv_timer_create(thermostat_weather_timer_cb, 700, NULL);
  lv_timer_create(thermostat_room_timer_cb, 900, NULL);
  lv_timer_create(thermostat_hvac_timer_cb, 1200, NULL);
}

static void thermostat_weather_timer_cb(lv_timer_t *timer)
{
  LV_UNUSED(timer);
  thermostat_update_weather_data();
}

static void thermostat_room_timer_cb(lv_timer_t *timer)
{
  LV_UNUSED(timer);
  thermostat_update_room_data();
}

static void thermostat_hvac_timer_cb(lv_timer_t *timer)
{
  LV_UNUSED(timer);
  thermostat_update_hvac_data();
}

static void thermostat_update_weather_data(void)
{
  g_view_model.weather_temp_c = 5.0f + ((float)(rand() % 260) / 10.0f);
  g_view_model.weather_ready = true;
  thermostat_update_weather_group();
  thermostat_fade_in_widget(g_weather_icon);
  thermostat_fade_in_widget(g_weather_temp_label);
}

static void thermostat_update_room_data(void)
{
  g_view_model.room_temp_c = 18.0f + ((float)(rand() % 80) / 10.0f);
  g_view_model.room_ready = true;
  thermostat_update_room_group();
  thermostat_fade_in_widget(g_room_temp_label);
  thermostat_fade_in_widget(g_room_icon);
}

static void thermostat_update_hvac_data(void)
{
  g_view_model.hvac_heating_active = (rand() % 2) == 0;
  g_view_model.hvac_cooling_active = !g_view_model.hvac_heating_active && (rand() % 2) == 0;
  g_view_model.hvac_ready = true;
  thermostat_update_hvac_status_group();
  thermostat_fade_in_widget(g_hvac_status_label);
}

static void thermostat_fade_in_widget(lv_obj_t *obj)
{
  if(obj == NULL) return;
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

static void thermostat_fan_spin_exec_cb(void *obj, int32_t value)
{
  lv_img_set_angle((lv_obj_t *)obj, (int16_t)value);
}

static lv_coord_t thermostat_scale_coord(int base_value)
{
  return (lv_coord_t)lrintf(base_value * g_layout_scale);
}

static lv_coord_t thermostat_scale_length(int base_value)
{
  return (lv_coord_t)lrintf(base_value * g_layout_scale);
}

static int thermostat_to_base_y(int screen_y)
{
  return (int)lrintf(screen_y / g_layout_scale);
}

static void thermostat_mode_icon_event(lv_event_t *e)
{
  if(lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  thermostat_target_t new_target = (g_view_model.active_target == THERMOSTAT_TARGET_COOL)
                                     ? THERMOSTAT_TARGET_HEAT
                                     : THERMOSTAT_TARGET_COOL;
  thermostat_select_setpoint_target(new_target);
  thermostat_update_action_bar_visuals();
}

static void thermostat_power_icon_event(lv_event_t *e)
{
  if(lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  g_view_model.system_powered = !g_view_model.system_powered;
  if(!g_view_model.system_powered) {
    g_view_model.fan_running = false;
  }
  thermostat_update_action_bar_visuals();
}

static void thermostat_fan_icon_event(lv_event_t *e)
{
  if(lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  if(!g_view_model.system_powered) {
    return;
  }
  g_view_model.fan_running = !g_view_model.fan_running;
  thermostat_update_action_bar_visuals();
}

static void thermostat_ui_init(void)
{
  thermostat_vm_init();

  if(!thermostat_fonts_init()) {
    fprintf(stderr, "[thermostat] Font initialization failed; falling back to default LVGL font\n");
  }

  thermostat_theme_init();
  g_root_screen = thermostat_create_root_screen();
  lv_scr_load(g_root_screen);
  lv_disp_t *disp = lv_disp_get_default();
  if(disp) {
    g_layout_scale = (float)lv_disp_get_ver_res(disp) / THERMOSTAT_TRACK_PANEL_HEIGHT;
    if(g_layout_scale <= 0.0f) {
      g_layout_scale = 1.0f;
    }
  } else {
    g_layout_scale = 1.0f;
  }
  g_layer_top = lv_layer_top();

  g_top_bar = thermostat_create_top_bar(g_root_screen);
  thermostat_create_weather_group(g_top_bar);
  thermostat_create_hvac_status_group(g_top_bar);
  thermostat_create_room_group(g_top_bar);
  thermostat_schedule_top_bar_updates();
  thermostat_create_tracks(g_root_screen);
  thermostat_create_setpoint_group(g_layer_top);
  thermostat_create_touch_zone(g_root_screen);
  thermostat_create_action_bar(g_root_screen);

  g_ui_initialized = true;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/
static void thermostat_fade_exec_cb(void *var, int32_t value);
