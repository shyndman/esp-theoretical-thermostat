#pragma once

#include <stdbool.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
  lv_flex_align_t main_place;
  lv_flex_align_t cross_place;
  lv_flex_align_t track_place;
  lv_coord_t width;
  lv_coord_t height;
  lv_coord_t pad_column;
  bool pad_left;
  bool pad_right;
  lv_coord_t pad_left_px;
  lv_coord_t pad_right_px;
} thermostat_setpoint_container_config_t;

typedef struct
{
  const lv_font_t *font;
  lv_color_t color;
  lv_coord_t translate_x;
  lv_coord_t translate_y;
} thermostat_setpoint_label_config_t;

typedef struct
{
  lv_obj_t *container;
  lv_obj_t *whole_label;
  lv_obj_t *fraction_label;
  lv_obj_t *track;
  bool is_active;
  bool setpoint_valid;
  lv_color_t color_active;
  lv_color_t color_inactive;
  lv_opa_t label_opa_active;
  lv_opa_t label_opa_inactive;
  lv_opa_t track_opa_active;
  lv_opa_t track_opa_inactive;
} thermostat_setpoint_active_style_t;

typedef struct
{
  lv_obj_t *whole_label;
  lv_obj_t *fraction_label;
  lv_color_t color_valid;
} thermostat_setpoint_label_pair_t;

void thermostat_ui_reset_container(lv_obj_t *obj);
lv_obj_t *thermostat_setpoint_create_track(lv_obj_t *parent, lv_color_t color);
lv_obj_t *thermostat_setpoint_create_container(lv_obj_t *parent,
                                               const thermostat_setpoint_container_config_t *config);
lv_obj_t *thermostat_setpoint_create_label(lv_obj_t *parent,
                                           const thermostat_setpoint_label_config_t *config);
void thermostat_setpoint_apply_active_styles(const thermostat_setpoint_active_style_t *style);
void thermostat_setpoint_update_value_labels(const thermostat_setpoint_label_pair_t *labels,
                                             bool is_valid,
                                             float value_c);

#ifdef __cplusplus
}
#endif
