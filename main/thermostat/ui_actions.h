#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

void thermostat_create_action_bar(lv_obj_t *parent);
void thermostat_update_action_bar_visuals(void);
void thermostat_mode_icon_event(lv_event_t *e);
void thermostat_power_icon_event(lv_event_t *e);
void thermostat_fan_icon_event(lv_event_t *e);
void thermostat_fan_spin_exec_cb(void *obj, int32_t value);
lv_obj_t *thermostat_get_action_bar(void);
lv_obj_t *thermostat_get_mode_icon(void);
lv_obj_t *thermostat_get_power_icon(void);
lv_obj_t *thermostat_get_fan_icon(void);

#ifdef __cplusplus
}
#endif
