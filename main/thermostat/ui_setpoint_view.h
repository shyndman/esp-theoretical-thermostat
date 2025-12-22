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
int thermostat_clamp_track_y(int y);
float thermostat_temperature_from_y(int track_y);
int thermostat_track_y_from_temperature(float temp);
int thermostat_compute_label_y(int track_y);
int thermostat_compute_track_height(int track_y);
void thermostat_compute_state_from_temperature(float temp, thermostat_slider_state_t *state);
void thermostat_compute_state_from_y(int sample_y, thermostat_slider_state_t *state);
void thermostat_create_tracks(lv_obj_t *parent);
void thermostat_update_track_geometry(void);
void thermostat_create_setpoint_group(lv_obj_t *parent);
void thermostat_update_setpoint_labels(void);
void thermostat_update_active_setpoint_styles(void);
void thermostat_format_setpoint(float value,
                                char *whole_buf,
                                size_t whole_buf_sz,
                                char *fraction_buf,
                                size_t fraction_buf_sz);
void thermostat_position_setpoint_labels(void);
bool thermostat_get_setpoint_stripe(thermostat_target_t target, lv_area_t *stripe);
lv_obj_t *thermostat_get_setpoint_track(thermostat_target_t target);
lv_obj_t *thermostat_get_setpoint_group(void);
lv_obj_t *thermostat_get_setpoint_container(thermostat_target_t target);
lv_obj_t *thermostat_get_setpoint_label(thermostat_target_t target);
lv_obj_t *thermostat_get_cooling_label(void);
lv_obj_t *thermostat_get_cooling_fraction_label(void);
lv_obj_t *thermostat_get_heating_label(void);
lv_obj_t *thermostat_get_heating_fraction_label(void);

#ifdef __cplusplus
}
#endif
