#include "thermostat/backlight_manager.h"

#include <inttypes.h>
#include <string.h>
#include <time.h>
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "sdkconfig.h"
#include "bsp/display.h"
#include "esp_lv_adapter.h"

#define SEC_TO_US(s)        ((int64_t)(s) * 1000000LL)
#define DAYPART_PERIOD_US   (10 * 1000000ULL)
#define SCHEDULE_PERIOD_US  (10 * 1000000ULL)

typedef struct {
    lv_display_t *disp;
    bool initialized;
    bool ui_ready;
    bool idle_sleep_active;
    bool antiburn_active;
    bool antiburn_manual;
    bool night_mode;
    bool backlight_lit;
    esp_timer_handle_t idle_timer;
    esp_timer_handle_t daypart_timer;
    esp_timer_handle_t schedule_timer;
    esp_timer_handle_t antiburn_timer;
} backlight_state_t;

static const char *TAG = "backlight";
static backlight_state_t s_state;

static void idle_timer_cb(void *arg);
static void daypart_timer_cb(void *arg);
static void schedule_timer_cb(void *arg);
static void antiburn_timer_cb(void *arg);
static void schedule_idle_timer(void);
static void enter_idle_state(void);
static void exit_idle_state(const char *reason);
static void apply_current_brightness(const char *reason);
static void update_daypart(bool log_current);
static void handle_schedule_tick(void);
static void poke_lvgl_activity(const char *reason);
static const char *wake_reason_name(backlight_wake_reason_t reason);
static const char *format_now(char *buf, size_t buf_size);

static inline int clamp_percent(int value)
{
    if (value < 1) {
        return 1;
    }
    if (value > 100) {
        return 100;
    }
    return value;
}

esp_err_t backlight_manager_init(const backlight_manager_config_t *config)
{
    ESP_RETURN_ON_FALSE(config != NULL, ESP_ERR_INVALID_ARG, TAG, "config is NULL");
    ESP_RETURN_ON_FALSE(config->disp != NULL, ESP_ERR_INVALID_ARG, TAG, "LVGL display handle required");
    memset(&s_state, 0, sizeof(s_state));
    s_state.disp = config->disp;

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

    update_daypart(true);
    apply_current_brightness("init");
    schedule_idle_timer();
    ESP_RETURN_ON_ERROR(esp_timer_start_periodic(s_state.daypart_timer, DAYPART_PERIOD_US), TAG,
                        "start daypart periodic failed");
    ESP_RETURN_ON_ERROR(esp_timer_start_periodic(s_state.schedule_timer, SCHEDULE_PERIOD_US), TAG,
                        "start schedule periodic failed");

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
    backlight_manager_notify_interaction(BACKLIGHT_WAKE_REASON_BOOT);
}

void backlight_manager_notify_interaction(backlight_wake_reason_t reason)
{
    if (!s_state.initialized) {
        return;
    }

    ESP_LOGI(TAG, "[idle] interaction reason=%s antiburn=%d idle=%d",
             wake_reason_name(reason), s_state.antiburn_active, s_state.idle_sleep_active);

    if (s_state.antiburn_active) {
        ESP_LOGI(TAG, "[antiburn] Interaction detected, stopping pixel training");
        backlight_manager_set_antiburn(false, false);
    }

    if (s_state.idle_sleep_active) {
        exit_idle_state("interaction");
    }

    poke_lvgl_activity("interaction");
    schedule_idle_timer();
}

esp_err_t backlight_manager_set_antiburn(bool enable, bool manual)
{
    ESP_RETURN_ON_FALSE(s_state.initialized, ESP_ERR_INVALID_STATE, TAG, "not initialized");

    if (enable) {
        if (s_state.antiburn_active) {
            s_state.antiburn_manual = s_state.antiburn_manual || manual;
            ESP_LOGI(TAG, "[antiburn] already active (manual=%d)", s_state.antiburn_manual);
            return ESP_OK;
        }
        s_state.antiburn_active = true;
        s_state.antiburn_manual = manual;
        s_state.idle_sleep_active = false;
        char ts[16] = {0};
        ESP_LOGI(TAG, "[antiburn] Starting pixel training (%s) @ %s",
                 manual ? "manual" : "scheduled",
                 format_now(ts, sizeof(ts)));
        esp_timer_stop(s_state.idle_timer);
        if (s_state.backlight_lit) {
            esp_err_t err = bsp_display_backlight_off();
            if (err == ESP_OK) {
                s_state.backlight_lit = false;
            } else {
                ESP_LOGW(TAG, "[antiburn] backlight off failed: %s", esp_err_to_name(err));
            }
        }
        esp_timer_stop(s_state.antiburn_timer);
        ESP_RETURN_ON_ERROR(esp_timer_start_once(s_state.antiburn_timer,
                        SEC_TO_US(CONFIG_THEO_ANTIBURN_DURATION_SECONDS)),
                        TAG, "antiburn duration timer start failed");
    } else {
        if (!s_state.antiburn_active) {
            return ESP_OK;
        }
        s_state.antiburn_active = false;
        s_state.antiburn_manual = false;
        char ts[16] = {0};
        ESP_LOGI(TAG, "[antiburn] Stopping pixel training @ %s", format_now(ts, sizeof(ts)));
        esp_timer_stop(s_state.antiburn_timer);
        apply_current_brightness("antiburn-stop");
        schedule_idle_timer();
    }

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

static void idle_timer_cb(void *arg)
{
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

static void schedule_idle_timer(void)
{
    if (!s_state.initialized) {
        return;
    }
    esp_timer_stop(s_state.idle_timer);
    ESP_ERROR_CHECK_WITHOUT_ABORT(
        esp_timer_start_once(s_state.idle_timer, SEC_TO_US(CONFIG_THEO_BACKLIGHT_TIMEOUT_SECONDS)));
    ESP_LOGI(TAG, "[idle] timer rescheduled for %ds", CONFIG_THEO_BACKLIGHT_TIMEOUT_SECONDS);
}

static void enter_idle_state(void)
{
    if (s_state.idle_sleep_active || s_state.antiburn_active) {
        return;
    }
    s_state.idle_sleep_active = true;
    ESP_LOGI(TAG, "[idle] timeout reached; turning off backlight");
    if (s_state.backlight_lit) {
        esp_err_t err = bsp_display_backlight_off();
        if (err == ESP_OK) {
            s_state.backlight_lit = false;
        } else {
            ESP_LOGW(TAG, "[idle] backlight off failed: %s", esp_err_to_name(err));
        }
    }
}

static void exit_idle_state(const char *reason)
{
    if (!s_state.idle_sleep_active) {
        return;
    }
    s_state.idle_sleep_active = false;
    ESP_LOGI(TAG, "[idle] exiting idle sleep via %s", reason ? reason : "unknown");
    apply_current_brightness("resume");
}

static void apply_current_brightness(const char *reason)
{
    int percent = s_state.night_mode ? CONFIG_THEO_BACKLIGHT_NIGHT_BRIGHTNESS_PERCENT
                                     : CONFIG_THEO_BACKLIGHT_DAY_BRIGHTNESS_PERCENT;
    percent = clamp_percent(percent);
    esp_err_t err = bsp_display_backlight_on();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "[backlight] enable failed: %s", esp_err_to_name(err));
    }
    err = bsp_display_brightness_set(percent);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "[backlight] brightness set failed: %s", esp_err_to_name(err));
    } else {
        s_state.backlight_lit = true;
        ESP_LOGI(TAG, "[backlight] set to %d%% (%s)", percent, reason ? reason : "update");
    }
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

static void handle_schedule_tick(void)
{
    time_t now = 0;
    time(&now);
    struct tm local_time = {0};
    if (localtime_r(&now, &local_time) == NULL) {
        return;
    }
    int minutes = local_time.tm_hour * 60 + local_time.tm_min;
    bool in_window = false;
    int start = CONFIG_THEO_ANTIBURN_SCHEDULE_START_MINUTE;
    int stop = CONFIG_THEO_ANTIBURN_SCHEDULE_STOP_MINUTE;
    if (start <= stop) {
        in_window = (minutes >= start) && (minutes < stop);
    } else {
        in_window = (minutes >= start) || (minutes < stop);
    }

    if (in_window && !s_state.antiburn_active) {
        char ts[16] = {0};
        ESP_LOGI(TAG, "[antiburn] schedule window entered @ %s", format_now(ts, sizeof(ts)));
        backlight_manager_set_antiburn(true, false);
    } else if (!in_window && s_state.antiburn_active && !s_state.antiburn_manual) {
        char ts[16] = {0};
        ESP_LOGI(TAG, "[antiburn] schedule window ended @ %s", format_now(ts, sizeof(ts)));
        backlight_manager_set_antiburn(false, false);
    }
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
        ESP_LOGI(TAG, "[lvgl] activity trig (%s)", reason ? reason : "wake");
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
    default:
        return "unknown";
    }
}
