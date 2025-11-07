#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

lv_obj_t *thermostat_create_top_bar(lv_obj_t *parent);
void thermostat_create_weather_group(lv_obj_t *parent);
void thermostat_update_weather_group(void);
void thermostat_create_hvac_status_group(lv_obj_t *parent);
void thermostat_update_hvac_status_group(void);
void thermostat_create_room_group(lv_obj_t *parent);
void thermostat_update_room_group(void);
void thermostat_schedule_top_bar_updates(void);
void thermostat_weather_timer_cb(lv_timer_t *timer);
void thermostat_room_timer_cb(lv_timer_t *timer);
void thermostat_hvac_timer_cb(lv_timer_t *timer);
void thermostat_update_weather_data(void);
void thermostat_update_room_data(void);
void thermostat_update_hvac_data(void);

#ifdef __cplusplus
}
#endif
