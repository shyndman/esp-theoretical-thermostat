#pragma once

#include <stddef.h>
#include "lvgl.h"
#include "thermostat/ui_state.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
  int track_y;
  int track_height;
  int label_y;
  float setpoint;
} thermostat_slider_state_t;

float thermostat_clamp_temperature(float value);
float thermostat_round_to_step(float value);
int thermostat_clamp_track_y(int y);
float thermostat_temperature_from_y(int track_y);
int thermostat_track_y_from_temperature(float temp);
int thermostat_compute_label_y(int track_y);
int thermostat_compute_track_height(int track_y);
void thermostat_compute_state_from_temperature(float temp, thermostat_slider_state_t *state);
void thermostat_compute_state_from_y(int sample_y, thermostat_slider_state_t *state);
void thermostat_create_tracks(lv_obj_t *parent);
void thermostat_update_track_geometry(void);
void thermostat_update_layer_order(void);
void thermostat_create_setpoint_group(lv_obj_t *parent);
void thermostat_update_setpoint_labels(void);
void thermostat_update_active_setpoint_styles(void);
void thermostat_format_setpoint(float value,
                                char *whole_buf,
                                size_t whole_buf_sz,
                                char *fraction_buf,
                                size_t fraction_buf_sz);
void thermostat_position_setpoint_labels(void);
lv_coord_t thermostat_scale_coord(int base_value);
lv_coord_t thermostat_scale_length(int base_value);

#ifdef __cplusplus
}
#endif
