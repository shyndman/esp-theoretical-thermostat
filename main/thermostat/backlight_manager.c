#include "thermostat/backlight_manager.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "sdkconfig.h"
#include "bsp/display.h"
#include "esp_lv_adapter.h"
#include "sensors/radar_presence.h"
#include "thermostat/thermostat_led_status.h"

#define SEC_TO_US(s)           ((int64_t)(s) * 1000000LL)
#define MS_TO_US(ms)           ((int64_t)(ms) * 1000LL)
#define DAYPART_PERIOD_US      (10 * 1000000ULL)
#define SCHEDULE_PERIOD_US     (10 * 1000000ULL)
#define PRESENCE_POLL_US       MS_TO_US(CONFIG_THEO_RADAR_POLL_INTERVAL_MS)

/* Try to match LVGL's default refresh period (ms) so we don't fight the scheduler. */
#ifdef CONFIG_LV_DEF_REFR_PERIOD
#define SNOW_TIMER_PERIOD_MS   CONFIG_LV_DEF_REFR_PERIOD
#else
#define SNOW_TIMER_PERIOD_MS   (15)
#endif

#define BACKLIGHT_FADE_MS       (500)
#define BACKLIGHT_FADE_STEP_MS  (20)
#define MQTT_ANTIBURN_DURATION_SECONDS (10)

typedef enum {
    BACKLIGHT_EASING_LINEAR = 0,
    BACKLIGHT_EASING_EASE_IN,
} backlight_easing_t;

typedef struct {
    lv_display_t *disp;
    bool initialized;
    bool ui_ready;
    bool idle_sleep_active;
    bool antiburn_active;
    bool antiburn_manual;
    bool antiburn_schedule_window_consumed;
    bool night_mode;
    bool backlight_lit;
    bool presence_ignored;
    uint32_t interaction_serial;
    esp_timer_handle_t idle_timer;
    esp_timer_handle_t daypart_timer;
    esp_timer_handle_t schedule_timer;
    esp_timer_handle_t antiburn_timer;
    esp_timer_handle_t fade_timer;
    lv_timer_t *snow_timer;
    lv_obj_t *snow_overlay;
    bool snow_running;
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
static void schedule_timer_cb(void *arg);
static void antiburn_timer_cb(void *arg);
static void snow_timer_cb(lv_timer_t *timer);
static void presence_timer_cb(void *arg);
static bool antiburn_schedule_window_active(void);
static esp_err_t start_antiburn(bool manual, uint32_t duration_seconds, const char *source);
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
static void handle_schedule_tick(void);
static void poke_lvgl_activity(const char *reason);
static const char *wake_reason_name(backlight_wake_reason_t reason);
static const char *format_now(char *buf, size_t buf_size);
static bool snow_overlay_start(void);
static void snow_overlay_stop(void);
static void snow_draw_frame(void);
static void snow_overlay_draw_event(lv_event_t *e);
static void snow_overlay_touch_event(lv_event_t *e);

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

    esp_timer_create_args_t schedule_args = {
        .callback = schedule_timer_cb,
        .name = "theo_antiburn_sched",
    };
    ESP_RETURN_ON_ERROR(esp_timer_create(&schedule_args, &s_state.schedule_timer), TAG, "schedule timer create failed");

    esp_timer_create_args_t antiburn_args = {
        .callback = antiburn_timer_cb,
        .name = "theo_antiburn",
    };
    ESP_RETURN_ON_ERROR(esp_timer_create(&antiburn_args, &s_state.antiburn_timer), TAG, "antiburn timer create failed");

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
    ESP_RETURN_ON_ERROR(esp_timer_start_periodic(s_state.schedule_timer, SCHEDULE_PERIOD_US), TAG,
                        "start schedule periodic failed");
#ifdef CONFIG_THEO_RADAR_ENABLE
    ESP_RETURN_ON_ERROR(esp_timer_start_periodic(s_state.presence_timer, PRESENCE_POLL_US), TAG,
                        "start presence periodic failed");
#else
    ESP_LOGI(TAG, "[presence] Radar disabled; skipping presence timer");
#endif

    s_state.initialized = true;
    ESP_LOGI(TAG,
             "Backlight manager ready (timeout=%ds, antiburn=%ds, day=%d%%, night=%d%%)",
             CONFIG_THEO_BACKLIGHT_TIMEOUT_SECONDS,
             CONFIG_THEO_ANTIBURN_DURATION_SECONDS,
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
    ESP_LOGD(TAG, "[idle] interaction reason=%s antiburn=%d idle=%d",
             wake_reason_name(reason), s_state.antiburn_active, s_state.idle_sleep_active);

    if (s_state.antiburn_active) {
        if (reason == BACKLIGHT_WAKE_REASON_TOUCH) {
            ESP_LOGI(TAG, "[antiburn] touch ignored during pixel training");
        } else {
            ESP_LOGI(TAG, "[antiburn] Interaction detected, stopping pixel training");
            backlight_manager_set_antiburn(false, false);
        }
        consumed = true;
    }

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

static esp_err_t start_antiburn(bool manual, uint32_t duration_seconds, const char *source)
{
    ESP_RETURN_ON_FALSE(duration_seconds > 0, ESP_ERR_INVALID_ARG, TAG, "antiburn duration must be > 0");

    const char *trigger = source ? source : (manual ? "manual" : "scheduled");
    char ts[16] = {0};

    if (s_state.antiburn_active) {
        s_state.antiburn_manual = s_state.antiburn_manual || manual;
        s_state.remote_sleep_armed = false;
        esp_timer_stop(s_state.idle_timer);
        ESP_LOGI(TAG, "[antiburn] Restarting pixel training (%s) @ %s",
                 trigger,
                 format_now(ts, sizeof(ts)));
        if (!s_state.snow_running && !snow_overlay_start()) {
            ESP_LOGW(TAG, "[antiburn] snow overlay failed to start");
        }
        esp_timer_stop(s_state.antiburn_timer);
        esp_err_t err = esp_timer_start_once(s_state.antiburn_timer, SEC_TO_US(duration_seconds));
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "[antiburn] duration timer restart failed: %s", esp_err_to_name(err));
            backlight_manager_set_antiburn(false, false);
            return err;
        }
        return ESP_OK;
    }

    s_state.antiburn_active = true;
    s_state.antiburn_manual = manual;
    s_state.idle_sleep_active = false;
    s_state.remote_sleep_armed = false;
    ESP_LOGI(TAG, "[antiburn] Starting pixel training (%s) @ %s",
             trigger,
             format_now(ts, sizeof(ts)));
    esp_timer_stop(s_state.idle_timer);
    apply_current_brightness("antiburn-start");
    if (!snow_overlay_start()) {
        ESP_LOGW(TAG, "[antiburn] snow overlay failed to start");
    }
    esp_timer_stop(s_state.antiburn_timer);
    esp_err_t err = esp_timer_start_once(s_state.antiburn_timer, SEC_TO_US(duration_seconds));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "[antiburn] duration timer start failed: %s", esp_err_to_name(err));
        backlight_manager_set_antiburn(false, false);
        return err;
    }

    return ESP_OK;
}

esp_err_t backlight_manager_trigger_antiburn(void)
{
    ESP_RETURN_ON_FALSE(s_state.initialized, ESP_ERR_INVALID_STATE, TAG, "not initialized");

    if (antiburn_schedule_window_active()) {
        s_state.antiburn_schedule_window_consumed = true;
        ESP_LOGI(TAG, "[antiburn] command consumed current schedule window");
    }

    return start_antiburn(true, MQTT_ANTIBURN_DURATION_SECONDS, "command");
}

esp_err_t backlight_manager_set_antiburn(bool enable, bool manual)
{
    ESP_RETURN_ON_FALSE(s_state.initialized, ESP_ERR_INVALID_STATE, TAG, "not initialized");

    if (enable) {
        return start_antiburn(manual,
                              CONFIG_THEO_ANTIBURN_DURATION_SECONDS,
                              manual ? "manual" : "scheduled");
    } else {
        if (!s_state.antiburn_active) {
            return ESP_OK;
        }
        s_state.antiburn_active = false;
        s_state.antiburn_manual = false;
        s_state.remote_sleep_armed = false;
        char ts[16] = {0};
        ESP_LOGI(TAG, "[antiburn] Stopping pixel training @ %s", format_now(ts, sizeof(ts)));
        snow_overlay_stop();
        esp_timer_stop(s_state.antiburn_timer);
        apply_current_brightness("antiburn-stop");
        schedule_idle_timer();
    }

    return ESP_OK;
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

bool backlight_manager_is_antiburn_active(void)
{
    return s_state.antiburn_active;
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

static void schedule_timer_cb(void *arg)
{
    handle_schedule_tick();
}

static void antiburn_timer_cb(void *arg)
{
    ESP_LOGI(TAG, "[antiburn] duration elapsed; stopping automatically");
    backlight_manager_set_antiburn(false, false);
}

static void snow_timer_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);
    snow_draw_frame();
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

    // Skip presence detection during antiburn
    if (s_state.antiburn_active) {
        s_state.presence_wake_pending = false;
        s_state.presence_holding = false;
        s_state.presence_hold_active = false;
        s_state.presence_hold_start_us = 0;
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
    if (s_state.idle_sleep_active || s_state.antiburn_active || s_state.hold_active) {
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
    int percent = 0;
    if (s_state.antiburn_active) {
        percent = 100;
    } else {
        percent = s_state.night_mode ? CONFIG_THEO_BACKLIGHT_NIGHT_BRIGHTNESS_PERCENT
                                     : CONFIG_THEO_BACKLIGHT_DAY_BRIGHTNESS_PERCENT;
    }
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
        if (!s_state.idle_sleep_active && !s_state.antiburn_active) {
            apply_current_brightness("daypart");
        }
    }
}

static bool antiburn_schedule_window_active(void)
{
    time_t now = 0;
    time(&now);
    struct tm local_time = {0};
    if (localtime_r(&now, &local_time) == NULL) {
        return false;
    }

    int minutes = local_time.tm_hour * 60 + local_time.tm_min;
    int start = CONFIG_THEO_ANTIBURN_SCHEDULE_START_MINUTE;
    int stop = CONFIG_THEO_ANTIBURN_SCHEDULE_STOP_MINUTE;
    if (start <= stop) {
        return (minutes >= start) && (minutes < stop);
    }

    return (minutes >= start) || (minutes < stop);
}

static void handle_schedule_tick(void)
{
    bool in_window = antiburn_schedule_window_active();

    if (!in_window) {
        s_state.antiburn_schedule_window_consumed = false;
        if (s_state.antiburn_active && !s_state.antiburn_manual) {
            char ts[16] = {0};
            ESP_LOGI(TAG, "[antiburn] schedule window ended @ %s", format_now(ts, sizeof(ts)));
            backlight_manager_set_antiburn(false, false);
        }
        return;
    }

    if (!s_state.antiburn_active && !s_state.antiburn_schedule_window_consumed) {
        char ts[16] = {0};
        ESP_LOGI(TAG, "[antiburn] schedule window entered @ %s", format_now(ts, sizeof(ts)));
        esp_err_t err = backlight_manager_set_antiburn(true, false);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "[antiburn] scheduled start failed: %s", esp_err_to_name(err));
            return;
        }
        s_state.antiburn_schedule_window_consumed = true;
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

static bool snow_overlay_start(void)
{
    if (s_state.snow_running) {
        return true;
    }
    if (s_state.disp == NULL) {
        return false;
    }

    const int hor = lv_display_get_horizontal_resolution(s_state.disp);
    const int ver = lv_display_get_vertical_resolution(s_state.disp);
    if (hor <= 0 || ver <= 0) {
        return false;
    }

    if (esp_lv_adapter_lock(-1) != ESP_OK) {
        ESP_LOGW(TAG, "[snow] failed to lock LVGL for overlay");
        return false;
    }

    if (s_state.snow_overlay == NULL) {
        s_state.snow_overlay = lv_obj_create(lv_layer_top());
        lv_obj_remove_style_all(s_state.snow_overlay);
        lv_obj_clear_flag(s_state.snow_overlay, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_color(s_state.snow_overlay, lv_color_hex(0x000000), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(s_state.snow_overlay, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_add_flag(s_state.snow_overlay, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(s_state.snow_overlay, LV_OBJ_FLAG_PRESS_LOCK);

        /* Draw-time static noise effect */
        lv_obj_add_event_cb(s_state.snow_overlay, snow_overlay_draw_event, LV_EVENT_DRAW_MAIN, NULL);

        /* Touch is ignored/consumed during anti-burn */
        lv_obj_add_event_cb(s_state.snow_overlay, snow_overlay_touch_event, LV_EVENT_PRESSED, NULL);
        lv_obj_add_event_cb(s_state.snow_overlay, snow_overlay_touch_event, LV_EVENT_PRESSING, NULL);
        lv_obj_add_event_cb(s_state.snow_overlay, snow_overlay_touch_event, LV_EVENT_RELEASED, NULL);
        lv_obj_add_event_cb(s_state.snow_overlay, snow_overlay_touch_event, LV_EVENT_PRESS_LOST, NULL);
        lv_obj_add_event_cb(s_state.snow_overlay, snow_overlay_touch_event, LV_EVENT_GESTURE, NULL);
        lv_obj_add_event_cb(s_state.snow_overlay, snow_overlay_touch_event, LV_EVENT_CLICKED, NULL);
    }

    lv_obj_set_size(s_state.snow_overlay, hor, ver);
    lv_obj_move_foreground(s_state.snow_overlay);
    lv_obj_invalidate(s_state.snow_overlay);

    if (s_state.snow_timer == NULL) {
        s_state.snow_timer = lv_timer_create(snow_timer_cb, SNOW_TIMER_PERIOD_MS, NULL);
        if (s_state.snow_timer == NULL) {
            esp_lv_adapter_unlock();
            ESP_LOGE(TAG, "[snow] lv_timer_create failed");
            snow_overlay_stop();
            return false;
        }
        lv_timer_set_repeat_count(s_state.snow_timer, -1);
    }

    esp_lv_adapter_unlock();
    s_state.snow_running = true;
    ESP_LOGI(TAG, "[snow] overlay running (%dx%d)", hor, ver);
    return true;
}

static void snow_overlay_stop(void)
{
    if (s_state.snow_timer) {
        lv_timer_del(s_state.snow_timer);
        s_state.snow_timer = NULL;
    }
    s_state.snow_running = false;

    if (esp_lv_adapter_lock(-1) == ESP_OK) {
        if (s_state.snow_overlay) {
            lv_obj_del(s_state.snow_overlay);
            s_state.snow_overlay = NULL;
        }
        esp_lv_adapter_unlock();
    }
    ESP_LOGI(TAG, "[snow] overlay stopped");
}

static void snow_draw_frame(void)
{
    if (!s_state.snow_running || s_state.snow_overlay == NULL) {
        return;
    }

    /* Request a redraw; the draw handler generates new static per refresh. */
    lv_obj_invalidate(s_state.snow_overlay);
}

static uint32_t snow_xorshift32(uint32_t x)
{
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return x;
}

static void snow_overlay_draw_event(lv_event_t *e)
{
    if (!s_state.snow_running) {
        return;
    }
    lv_layer_t *layer = lv_event_get_layer(e);
    if (layer == NULL || layer->draw_buf == NULL || layer->draw_buf->data == NULL) {
        return;
    }

    lv_area_t clip = layer->_clip_area;
    if (clip.x1 < layer->buf_area.x1) clip.x1 = layer->buf_area.x1;
    if (clip.y1 < layer->buf_area.y1) clip.y1 = layer->buf_area.y1;
    if (clip.x2 > layer->buf_area.x2) clip.x2 = layer->buf_area.x2;
    if (clip.y2 > layer->buf_area.y2) clip.y2 = layer->buf_area.y2;
    if (clip.x1 > clip.x2 || clip.y1 > clip.y2) {
        return;
    }

    const lv_color_format_t cf = layer->color_format;
    const int32_t stride = (int32_t)layer->draw_buf->header.stride;
    const int32_t buf_x0 = layer->buf_area.x1;
    const int32_t buf_y0 = layer->buf_area.y1;
    const int32_t x0 = clip.x1 - buf_x0;
    const int32_t y0 = clip.y1 - buf_y0;
    const int32_t width = clip.x2 - clip.x1 + 1;
    const int32_t height = clip.y2 - clip.y1 + 1;
    if (width <= 0 || height <= 0) {
        return;
    }

    uint32_t rng = esp_random();
    uint8_t *base = layer->draw_buf->data;

    /* Per-pixel noise; each pixel is pure red/green/blue/white. */
    if (cf == LV_COLOR_FORMAT_RGB565) {
        static const uint16_t colors[4] = {
            0xF800, /* red */
            0x07E0, /* green */
            0x001F, /* blue */
            0xFFFF, /* white */
        };
        for (int32_t y = 0; y < height; ++y) {
            uint16_t *row = (uint16_t *)(base + (size_t)(y0 + y) * (size_t)stride) + x0;
            for (int32_t x = 0; x < width; ++x) {
                rng = snow_xorshift32(rng);
                row[x] = colors[rng & 0x03];
            }
        }
        return;
    }

    if (cf == LV_COLOR_FORMAT_RGB888) {
        static const lv_color_t colors[4] = {
            { .blue = 0,   .green = 0,   .red = 255 }, /* red */
            { .blue = 0,   .green = 255, .red = 0   }, /* green */
            { .blue = 255, .green = 0,   .red = 0   }, /* blue */
            { .blue = 255, .green = 255, .red = 255 }, /* white */
        };
        for (int32_t y = 0; y < height; ++y) {
            uint8_t *row = base + (size_t)(y0 + y) * (size_t)stride + (size_t)x0 * 3U;
            for (int32_t x = 0; x < width; ++x) {
                rng = snow_xorshift32(rng);
                const lv_color_t c = colors[rng & 0x03];
                row[0] = c.blue;
                row[1] = c.green;
                row[2] = c.red;
                row += 3;
            }
        }
        return;
    }

    if (cf == LV_COLOR_FORMAT_XRGB8888 ||
        cf == LV_COLOR_FORMAT_ARGB8888 ||
        cf == LV_COLOR_FORMAT_ARGB8888_PREMULTIPLIED) {
        static const lv_color32_t colors[4] = {
            { .blue = 0,   .green = 0,   .red = 255, .alpha = 255 }, /* red */
            { .blue = 0,   .green = 255, .red = 0,   .alpha = 255 }, /* green */
            { .blue = 255, .green = 0,   .red = 0,   .alpha = 255 }, /* blue */
            { .blue = 255, .green = 255, .red = 255, .alpha = 255 }, /* white */
        };
        for (int32_t y = 0; y < height; ++y) {
            lv_color32_t *row = (lv_color32_t *)(base + (size_t)(y0 + y) * (size_t)stride) + x0;
            for (int32_t x = 0; x < width; ++x) {
                rng = snow_xorshift32(rng);
                row[x] = colors[rng & 0x03];
            }
        }
        return;
    }
}

static void snow_overlay_touch_event(lv_event_t *e)
{
    /* Do not accept touch-driven interactions during anti-burn. */
    lv_event_stop_processing(e);
    lv_event_stop_bubbling(e);
}

static const char *format_now(char *buf, size_t buf_size)
{
    time_t now = 0;
    time(&now);
    struct tm local_time = {0};
    if (localtime_r(&now, &local_time) == NULL) {
        snprintf(buf, buf_size, "--:--:--");
    } else {
        strftime(buf, buf_size, "%H:%M:%S", &local_time);
    }
    return buf;
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
