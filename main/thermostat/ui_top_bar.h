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

#ifdef __cplusplus
}
#endif
