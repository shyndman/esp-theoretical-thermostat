#pragma once

#include <stdbool.h>
#include "lvgl.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BACKLIGHT_WAKE_REASON_BOOT = 0,
    BACKLIGHT_WAKE_REASON_TOUCH,
    BACKLIGHT_WAKE_REASON_REMOTE,
    BACKLIGHT_WAKE_REASON_TIMER,
} backlight_wake_reason_t;

typedef struct {
    lv_display_t *disp;
} backlight_manager_config_t;

esp_err_t backlight_manager_init(const backlight_manager_config_t *config);
void backlight_manager_on_ui_ready(void);
bool backlight_manager_notify_interaction(backlight_wake_reason_t reason);
esp_err_t backlight_manager_set_antiburn(bool enable, bool manual);
bool backlight_manager_is_idle(void);
bool backlight_manager_is_antiburn_active(void);
bool backlight_manager_is_lit(void);
uint32_t backlight_manager_get_interaction_serial(void);
void backlight_manager_schedule_remote_sleep(uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif
