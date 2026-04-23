#include "thermostat/backlight_manager.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "sdkconfig.h"
#include "bsp/display.h"
#include "esp_lv_adapter.h"
#include "sensors/radar_presence.h"
#include "thermostat/thermostat_led_status.h"

#define SEC_TO_US(s)           ((int64_t)(s) * 1000000LL)
#define MS_TO_US(ms)           ((int64_t)(ms) * 1000LL)
#define DAYPART_PERIOD_US      (10 * 1000000ULL)
#define PRESENCE_POLL_US       MS_TO_US(CONFIG_THEO_RADAR_POLL_INTERVAL_MS)

#define BACKLIGHT_FADE_MS       (500)
#define BACKLIGHT_FADE_STEP_MS  (20)

typedef enum {
    BACKLIGHT_EASING_LINEAR = 0,
    BACKLIGHT_EASING_EASE_IN,
} backlight_easing_t;

typedef struct {
    lv_display_t *disp;
    bool initialized;
    bool ui_ready;
    bool idle_sleep_active;
    bool night_mode;
    bool backlight_lit;
    bool presence_ignored;
    uint32_t interaction_serial;
    esp_timer_handle_t idle_timer;
    esp_timer_handle_t daypart_timer;
    esp_timer_handle_t fade_timer;
    bool remote_sleep_armed;
    bool fade_active;
    int current_brightness_percent;
    int fade_start_percent;
    int fade_target_percent;
    int fade_step_count;
    int fade_steps_elapsed;
    backlight_easing_t fade_easing;
    char fade_reason[16];
    // Presence detection state
    esp_timer_handle_t presence_timer;
    bool presence_wake_pending;
    int64_t presence_first_close_us;
    bool presence_holding;
    int64_t presence_hold_start_us;
    bool presence_hold_active;
    // Forces the backlight on and suppresses idle timers while true.
    bool hold_active;
} backlight_state_t;

static const char *TAG = "backlight";
static backlight_state_t s_state;

static void idle_timer_cb(void *arg);
static void daypart_timer_cb(void *arg);
static void presence_timer_cb(void *arg);
static void schedule_idle_timer(void);
static void enter_idle_state(void);
static void exit_idle_state(const char *reason);
static void apply_current_brightness(const char *reason);
static void fade_timer_cb(void *arg);
static void start_backlight_fade(int target_percent, const char *reason);
static void stop_backlight_fade(void);
static void handle_fade_step(void);
static void set_brightness_immediate(int percent, const char *reason, bool log_info);
static void update_daypart(bool log_current);
static void poke_lvgl_activity(const char *reason);
static const char *wake_reason_name(backlight_wake_reason_t reason);

static inline int clamp_percent(int value)
{
    if (value < 0) {
        return 0;
    }
    if (value > 100) {
        return 100;
    }
    return value;
}

static float apply_easing(float t, backlight_easing_t easing)
{
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;

    switch (easing) {
    case BACKLIGHT_EASING_EASE_IN:
        return t * t;
    default:
        return t;
    }
}

esp_err_t backlight_manager_init(const backlight_manager_config_t *config)
{
    ESP_RETURN_ON_FALSE(config != NULL, ESP_ERR_INVALID_ARG, TAG, "config is NULL");
    ESP_RETURN_ON_FALSE(config->disp != NULL, ESP_ERR_INVALID_ARG, TAG, "LVGL display handle required");
    memset(&s_state, 0, sizeof(s_state));
    s_state.disp = config->disp;

    esp_err_t backlight_err = bsp_display_backlight_off();
    if (backlight_err != ESP_OK && backlight_err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "[backlight] initial off failed: %s", esp_err_to_name(backlight_err));
    }

    esp_timer_create_args_t idle_args = {
        .callback = idle_timer_cb,
        .name = "theo_idle",
    };
    ESP_RETURN_ON_ERROR(esp_timer_create(&idle_args, &s_state.idle_timer), TAG, "idle timer create failed");

    esp_timer_create_args_t daypart_args = {
        .callback = daypart_timer_cb,
        .name = "theo_daypart",
    };
    ESP_RETURN_ON_ERROR(esp_timer_create(&daypart_args, &s_state.daypart_timer), TAG, "daypart timer create failed");

    esp_timer_create_args_t fade_args = {
        .callback = fade_timer_cb,
        .name = "theo_backlight_fade",
    };
    ESP_RETURN_ON_ERROR(esp_timer_create(&fade_args, &s_state.fade_timer), TAG, "fade timer create failed");

    esp_timer_create_args_t presence_args = {
        .callback = presence_timer_cb,
        .name = "theo_presence",
    };
#ifdef CONFIG_THEO_RADAR_ENABLE
    ESP_RETURN_ON_ERROR(esp_timer_create(&presence_args, &s_state.presence_timer), TAG, "presence timer create failed");
#else
    s_state.presence_timer = NULL;
#endif

    update_daypart(true);
    apply_current_brightness("init");
    schedule_idle_timer();
    ESP_RETURN_ON_ERROR(esp_timer_start_periodic(s_state.daypart_timer, DAYPART_PERIOD_US), TAG,
                        "start daypart periodic failed");
#ifdef CONFIG_THEO_RADAR_ENABLE
    ESP_RETURN_ON_ERROR(esp_timer_start_periodic(s_state.presence_timer, PRESENCE_POLL_US), TAG,
                        "start presence periodic failed");
#else
    ESP_LOGI(TAG, "[presence] Radar disabled; skipping presence timer");
#endif

    s_state.initialized = true;
    ESP_LOGI(TAG,
             "Backlight manager ready (timeout=%ds, day=%d%%, night=%d%%)",
             CONFIG_THEO_BACKLIGHT_TIMEOUT_SECONDS,
             CONFIG_THEO_BACKLIGHT_DAY_BRIGHTNESS_PERCENT,
             CONFIG_THEO_BACKLIGHT_NIGHT_BRIGHTNESS_PERCENT);
    return ESP_OK;
}

void backlight_manager_on_ui_ready(void)
{
    if (!s_state.initialized || s_state.ui_ready) {
        return;
    }
    s_state.ui_ready = true;
    ESP_LOGI(TAG, "UI signaled ready; forcing backlight wake");
    (void)backlight_manager_notify_interaction(BACKLIGHT_WAKE_REASON_BOOT);
}

bool backlight_manager_notify_interaction(backlight_wake_reason_t reason)
{
    if (!s_state.initialized) {
        return false;
    }

    bool consumed = false;
    ESP_LOGD(TAG, "[idle] interaction reason=%s idle=%d",
             wake_reason_name(reason), s_state.idle_sleep_active);

    if (s_state.idle_sleep_active) {
        exit_idle_state("interaction");
        consumed = true;
    }

    if (reason == BACKLIGHT_WAKE_REASON_TOUCH ||
        reason == BACKLIGHT_WAKE_REASON_REMOTE ||
        reason == BACKLIGHT_WAKE_REASON_BOOT) {
        s_state.presence_hold_active = false;
        s_state.presence_hold_start_us = 0;
    }

    poke_lvgl_activity("interaction");
    s_state.interaction_serial++;
    s_state.remote_sleep_armed = false;
    schedule_idle_timer();
    return consumed;
}

esp_err_t backlight_manager_set_hold(bool enable)
{
    ESP_RETURN_ON_FALSE(s_state.initialized, ESP_ERR_INVALID_STATE, TAG, "not initialized");

    if (enable) {
        if (s_state.hold_active) {
            return ESP_OK;
        }
        s_state.hold_active = true;
        s_state.remote_sleep_armed = false;
        esp_timer_stop(s_state.idle_timer);
        if (s_state.idle_sleep_active) {
            exit_idle_state("hold");
        } else {
            apply_current_brightness("hold");
        }
        ESP_LOGI(TAG, "[hold] backlight hold enabled");
        return ESP_OK;
    }

    if (!s_state.hold_active) {
        return ESP_OK;
    }
    s_state.hold_active = false;
    ESP_LOGI(TAG, "[hold] backlight hold disabled");
    schedule_idle_timer();
    return ESP_OK;
}

bool backlight_manager_is_idle(void)
{
    return s_state.idle_sleep_active;
}

bool backlight_manager_is_lit(void)
{
    return s_state.backlight_lit;
}

uint32_t backlight_manager_get_interaction_serial(void)
{
    return s_state.interaction_serial;
}

void backlight_manager_schedule_remote_sleep(uint32_t timeout_ms)
{
    if (!s_state.initialized) {
        return;
    }
    if (timeout_ms == 0) {
        timeout_ms = 5000;
    }
    uint64_t delay_us = (uint64_t)timeout_ms * 1000ULL;
    esp_timer_stop(s_state.idle_timer);
    s_state.remote_sleep_armed = true;
    esp_err_t err = esp_timer_start_once(s_state.idle_timer, delay_us);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "[remote] sleep arm failed (%s)", esp_err_to_name(err));
        s_state.remote_sleep_armed = false;
        schedule_idle_timer();
        return;
    }
    ESP_LOGI(TAG, "[remote] backlight sleep armed for %ums", timeout_ms);
}

void backlight_manager_request_sleep(void)
{
    if (!s_state.initialized) {
        return;
    }
    ESP_LOGI(TAG, "[manual] sleep requested via power button");
    s_state.presence_ignored = true;
    enter_idle_state();
}

bool backlight_manager_is_presence_ignored(void)
{
    return s_state.presence_ignored;
}

static void idle_timer_cb(void *arg)
{
    LV_UNUSED(arg);

    // If presence is detected, don't enter idle - reschedule the timer instead
    if (s_state.presence_holding) {
        ESP_LOGD(TAG, "[idle] presence holding, rescheduling idle timer");
        schedule_idle_timer();
        return;
    }

    enter_idle_state();
}

static void daypart_timer_cb(void *arg)
{
    update_daypart(false);
}

#ifndef CONFIG_THEO_RADAR_ENABLE
static void presence_timer_cb(void *arg)
{
    LV_UNUSED(arg);
}
#else
static void presence_timer_cb(void *arg)
{
    LV_UNUSED(arg);

    if (!s_state.initialized) {
        return;
    }

    // Check if radar is online
    if (!radar_presence_is_online()) {
        // Radar offline - reset dwell and hold
        if (s_state.presence_wake_pending || s_state.presence_holding) {
            ESP_LOGD(TAG, "[presence] radar offline, resetting state");
        }
        s_state.presence_wake_pending = false;
        s_state.presence_holding = false;
        s_state.presence_hold_active = false;
        s_state.presence_hold_start_us = 0;
        return;
    }

    // Get radar state
    radar_presence_state_t radar_state;
    if (!radar_presence_get_state(&radar_state)) {
        s_state.presence_wake_pending = false;
        s_state.presence_holding = false;
        s_state.presence_hold_active = false;
        s_state.presence_hold_start_us = 0;
        return;
    }

    int64_t now_us = esp_timer_get_time();

    if (radar_state.presence_detected) {
        if (s_state.backlight_lit) {
            if (!s_state.presence_hold_active) {
                s_state.presence_hold_active = true;
                s_state.presence_hold_start_us = now_us;
            } else {
                int64_t hold_us = now_us - s_state.presence_hold_start_us;
                if (hold_us >= SEC_TO_US(CONFIG_THEO_BACKLIGHT_PRESENCE_MAX_SECONDS)) {
                    int64_t hold_seconds = hold_us / 1000000;
                    ESP_LOGI(TAG, "[presence] hold exceeded; forcing idle (%" PRId64 "s)",
                             hold_seconds);
                    s_state.presence_ignored = true;
                    s_state.presence_hold_active = false;
                    s_state.presence_hold_start_us = 0;
                    s_state.presence_holding = false;
                    s_state.presence_wake_pending = false;
                    enter_idle_state();
                    return;
                }
            }
        }

        // Check if target is within wake distance
        if (radar_state.detection_distance_cm < CONFIG_THEO_RADAR_WAKE_DISTANCE_CM) {
            if (!s_state.presence_wake_pending) {
                // Start dwell timer
                s_state.presence_wake_pending = true;
                s_state.presence_first_close_us = now_us;
                ESP_LOGD(TAG, "[presence] target entered close range (%u cm)", radar_state.detection_distance_cm);
            } else {
                // Check if dwell time reached
                int64_t dwell_us = now_us - s_state.presence_first_close_us;
                if (dwell_us >= MS_TO_US(CONFIG_THEO_RADAR_WAKE_DWELL_MS)) {
                    // Dwell time reached - wake if idle
                    if (s_state.idle_sleep_active) {
                        if (s_state.presence_ignored) {
                            ESP_LOGD(TAG, "[presence] wake suppressed (presence ignored)");
                        } else {
                            ESP_LOGI(TAG, "[presence] dwell complete, waking backlight");
                            exit_idle_state("presence");
                            poke_lvgl_activity("presence");
                            s_state.interaction_serial++;
                            schedule_idle_timer();
                        }
                    }
                    s_state.presence_wake_pending = false;
                }
            }
        } else {
            // Target moved away - reset dwell
            if (s_state.presence_wake_pending) {
                ESP_LOGD(TAG, "[presence] target left close range, resetting dwell");
            }
            s_state.presence_wake_pending = false;
        }

        // Any presence at any distance holds backlight awake
        s_state.presence_holding = true;
    } else {
        // No presence detected - reset everything
        if (s_state.presence_holding) {
            ESP_LOGD(TAG, "[presence] no presence, releasing hold");
        }
        s_state.presence_wake_pending = false;
        s_state.presence_holding = false;
        s_state.presence_hold_active = false;
        s_state.presence_hold_start_us = 0;
        if (s_state.presence_ignored) {
            s_state.presence_ignored = false;
            ESP_LOGI(TAG, "[presence] clearing presence ignore (no presence detected)");
        }
    }
}
#endif

static void schedule_idle_timer(void)
{
    if (!s_state.initialized) {
        return;
    }
    if (s_state.hold_active) {
        esp_timer_stop(s_state.idle_timer);
        ESP_LOGD(TAG, "[idle] hold active; timer suppressed");
        return;
    }
    esp_timer_stop(s_state.idle_timer);
    s_state.remote_sleep_armed = false;
    ESP_ERROR_CHECK_WITHOUT_ABORT(
        esp_timer_start_once(s_state.idle_timer, SEC_TO_US(CONFIG_THEO_BACKLIGHT_TIMEOUT_SECONDS)));
    ESP_LOGD(TAG, "[idle] timer rescheduled for %ds", CONFIG_THEO_BACKLIGHT_TIMEOUT_SECONDS);
}

static void enter_idle_state(void)
{
    if (s_state.idle_sleep_active || s_state.hold_active) {
        return;
    }
    s_state.idle_sleep_active = true;
    ESP_LOGI(TAG, "[idle] timeout reached; turning off backlight");
    s_state.remote_sleep_armed = false;
    start_backlight_fade(0, "idle");
    thermostat_led_status_on_screen_sleep();
}

static void exit_idle_state(const char *reason)
{
    if (!s_state.idle_sleep_active) {
        return;
    }
    s_state.idle_sleep_active = false;
    if (reason == NULL || strcmp(reason, "presence") != 0) {
        s_state.presence_ignored = false;
    }
    ESP_LOGI(TAG, "[idle] exiting idle sleep via %s", reason ? reason : "unknown");
    apply_current_brightness("resume");
    thermostat_led_status_on_screen_wake();
}

static void apply_current_brightness(const char *reason)
{
    int percent = s_state.night_mode ? CONFIG_THEO_BACKLIGHT_NIGHT_BRIGHTNESS_PERCENT
                                     : CONFIG_THEO_BACKLIGHT_DAY_BRIGHTNESS_PERCENT;
    percent = clamp_percent(percent);
    start_backlight_fade(percent, reason);
}

static void update_daypart(bool log_current)
{
    time_t now = 0;
    time(&now);
    struct tm local_time = {0};
    if (localtime_r(&now, &local_time) == NULL) {
        ESP_LOGI(TAG, "[daypart] time unavailable; keeping previous mode");
        return;
    }
    int minutes = local_time.tm_hour * 60 + local_time.tm_min;
    bool night = minutes < CONFIG_THEO_BACKLIGHT_OVERNIGHT_END_MINUTES;
    if (night != s_state.night_mode || log_current) {
        s_state.night_mode = night;
        ESP_LOGI(TAG, "[daypart] mode -> %s (minutes=%d)", night ? "overnight" : "daytime", minutes);
        if (!s_state.idle_sleep_active) {
            apply_current_brightness("daypart");
        }
    }
}

static void fade_timer_cb(void *arg)
{
    LV_UNUSED(arg);
    handle_fade_step();
}

static void stop_backlight_fade(void)
{
    if (!s_state.fade_timer || !s_state.fade_active) {
        s_state.fade_active = false;
        s_state.fade_step_count = 0;
        s_state.fade_steps_elapsed = 0;
        s_state.fade_reason[0] = '\0';
        return;
    }
    esp_err_t err = esp_timer_stop(s_state.fade_timer);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "[backlight] fade stop failed: %s", esp_err_to_name(err));
    }
    s_state.fade_active = false;
    s_state.fade_step_count = 0;
    s_state.fade_steps_elapsed = 0;
    s_state.fade_reason[0] = '\0';
}

static void set_brightness_immediate(int percent, const char *reason, bool log_info)
{
    percent = clamp_percent(percent);
    if (percent == 0) {
        esp_err_t err = bsp_display_backlight_off();
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            ESP_LOGW(TAG, "[backlight] backlight off failed: %s", esp_err_to_name(err));
        }
        s_state.current_brightness_percent = 0;
        s_state.backlight_lit = false;
        if (log_info) {
            ESP_LOGI(TAG, "[backlight] turned off (%s)", reason ? reason : "update");
        }
        return;
    }

    esp_err_t err = bsp_display_brightness_set(percent);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "[backlight] brightness set failed: %s", esp_err_to_name(err));
        return;
    }
    s_state.current_brightness_percent = percent;
    s_state.backlight_lit = true;
    if (log_info) {
        ESP_LOGI(TAG, "[backlight] set to %d%% (%s)", percent, reason ? reason : "update");
    }
}

static void start_backlight_fade(int target_percent, const char *reason)
{
    target_percent = clamp_percent(target_percent);
    int start_percent = s_state.backlight_lit ? s_state.current_brightness_percent : 0;

    if (target_percent == start_percent) {
        set_brightness_immediate(target_percent, reason, true);
        return;
    }

    stop_backlight_fade();

    s_state.fade_start_percent = start_percent;
    s_state.fade_target_percent = target_percent;
    s_state.fade_step_count = BACKLIGHT_FADE_MS / BACKLIGHT_FADE_STEP_MS;
    if (s_state.fade_step_count <= 0) {
        set_brightness_immediate(target_percent, reason, true);
        return;
    }
    s_state.fade_steps_elapsed = 0;
    s_state.fade_active = true;
    s_state.backlight_lit = true;
    s_state.fade_easing = (target_percent < start_percent) ? BACKLIGHT_EASING_EASE_IN
                                                           : BACKLIGHT_EASING_LINEAR;
    snprintf(s_state.fade_reason, sizeof(s_state.fade_reason), "%s", reason ? reason : "update");

    esp_err_t err = esp_timer_start_periodic(s_state.fade_timer, BACKLIGHT_FADE_STEP_MS * 1000ULL);
    if (err != ESP_OK) {
        s_state.fade_active = false;
        ESP_LOGW(TAG, "[backlight] fade start failed: %s", esp_err_to_name(err));
        set_brightness_immediate(target_percent, reason, true);
        return;
    }

    ESP_LOGI(TAG, "[backlight] fading to %d%% over %dms (%s)",
             target_percent,
             BACKLIGHT_FADE_MS,
             s_state.fade_reason);
    handle_fade_step();
}

static void handle_fade_step(void)
{
    if (!s_state.fade_active || s_state.fade_step_count <= 0) {
        return;
    }

    s_state.fade_steps_elapsed++;

    if (s_state.fade_steps_elapsed >= s_state.fade_step_count) {
        set_brightness_immediate(s_state.fade_target_percent, s_state.fade_reason, true);
        stop_backlight_fade();
        return;
    }

    int start_percent = s_state.fade_start_percent;
    int target_percent = s_state.fade_target_percent;
    int delta = target_percent - start_percent;
    int steps = s_state.fade_step_count;
    int elapsed = s_state.fade_steps_elapsed;

    float t = (float)elapsed / (float)steps;
    float eased_t = apply_easing(t, s_state.fade_easing);

    int next = start_percent;
    if (delta > 0) {
        next = start_percent + (int)((float)delta * eased_t);
        if (next <= s_state.current_brightness_percent) {
            next = s_state.current_brightness_percent + 1;
        }
        if (next > target_percent) {
            next = target_percent;
        }
    } else if (delta < 0) {
        next = start_percent + (int)((float)delta * eased_t);
        if (next >= s_state.current_brightness_percent) {
            next = s_state.current_brightness_percent - 1;
        }
        if (next < target_percent) {
            next = target_percent;
        }
    }

    set_brightness_immediate(next, NULL, false);
}

static void poke_lvgl_activity(const char *reason)
{
    if (s_state.disp == NULL) {
        return;
    }
    if (esp_lv_adapter_lock(0) == ESP_OK) {
        lv_disp_trig_activity(s_state.disp);
        ESP_LOGD(TAG, "[lvgl] activity trig (%s)", reason ? reason : "wake");
        esp_lv_adapter_unlock();
    } else {
        ESP_LOGW(TAG, "[lvgl] failed to acquire lock to trig activity");
    }
}

static const char *wake_reason_name(backlight_wake_reason_t reason)
{
    switch (reason) {
    case BACKLIGHT_WAKE_REASON_BOOT:
        return "boot";
    case BACKLIGHT_WAKE_REASON_TOUCH:
        return "touch";
    case BACKLIGHT_WAKE_REASON_REMOTE:
        return "remote";
    case BACKLIGHT_WAKE_REASON_TIMER:
        return "timer";
    case BACKLIGHT_WAKE_REASON_PRESENCE:
        return "presence";
    default:
        ESP_LOGW(TAG, "[wake] unknown reason=%d", reason);
        return "unknown";
    }
}
