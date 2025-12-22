#pragma once

#include <stdbool.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*thermostat_entrance_anim_complete_cb_t)(void *user_ctx);

void thermostat_entrance_anim_prepare(void);
void thermostat_entrance_anim_start(void);
bool thermostat_entrance_anim_is_active(void);
void thermostat_entrance_anim_set_complete_cb(thermostat_entrance_anim_complete_cb_t cb, void *user_ctx);

#ifdef __cplusplus
}
#endif
