#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct thermostat_splash thermostat_splash_t;
typedef void (*thermostat_splash_destroy_cb_t)(void *user_ctx);

thermostat_splash_t *thermostat_splash_create(lv_display_t *disp);
void thermostat_splash_destroy(thermostat_splash_t *splash,
                               thermostat_splash_destroy_cb_t on_destroy,
                               void *user_ctx);
void thermostat_splash_begin_white_fade(void);
void thermostat_splash_begin_fade(void);
esp_err_t thermostat_splash_set_status(thermostat_splash_t *splash, const char *status_text);
esp_err_t thermostat_splash_set_status_color(thermostat_splash_t *splash,
                                             const char *status_text,
                                             lv_color_t color);
esp_err_t thermostat_splash_finalize_status(thermostat_splash_t *splash,
                                            const char *status_text,
                                            lv_color_t color);
esp_err_t thermostat_splash_show_error(thermostat_splash_t *splash, const char *stage_name, esp_err_t err);
bool thermostat_splash_is_animating(thermostat_splash_t *splash);

#ifdef __cplusplus
}
#endif
