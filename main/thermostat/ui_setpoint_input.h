#pragma once

#include "lvgl.h"
#include "thermostat/ui_state.h"
#include "thermostat/ui_setpoint_view.h"

#ifdef __cplusplus
extern "C" {
#endif

lv_obj_t *thermostat_get_track_touch_zone(void);

float thermostat_clamp_cooling(float candidate, float heating_setpoint);
float thermostat_clamp_heating(float candidate, float cooling_setpoint);
void thermostat_apply_state_to_target(thermostat_target_t target, const thermostat_slider_state_t *state);
void thermostat_sync_active_slider_state(const thermostat_slider_state_t *state);
void thermostat_create_touch_zone(lv_obj_t *parent);
void thermostat_track_touch_event(lv_event_t *e);
void thermostat_handle_setpoint_event(lv_event_t *e);
void thermostat_select_setpoint_target(thermostat_target_t target);
void thermostat_commit_setpoints(void);
void thermostat_apply_setpoint_touch(int sample_y);
int thermostat_to_base_y(int screen_y);

#ifdef __cplusplus
}
#endif
