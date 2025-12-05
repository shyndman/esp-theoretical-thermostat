#include "thermostat/backlight_manager.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_random.h"
#include "sdkconfig.h"
#include "bsp/display.h"
#include "esp_lv_adapter.h"

#define SEC_TO_US(s)           ((int64_t)(s) * 1000000LL)
#define DAYPART_PERIOD_US      (10 * 1000000ULL)
#define SCHEDULE_PERIOD_US     (10 * 1000000ULL)
#define SNOW_TIMER_PERIOD_MS   (50) // 20 Hz
#define SNOW_MIN_TILE           4
#define SNOW_BUF_ALIGN          LV_DRAW_BUF_ALIGN
#define BACKLIGHT_FADE_MS       (1600)
#define BACKLIGHT_FADE_STEP_MS  (20)

typedef struct {
    lv_display_t *disp;
    bool initialized;
    bool ui_ready;
    bool idle_sleep_active;
    bool antiburn_active;
    bool antiburn_manual;
    bool night_mode;
    bool backlight_lit;
    uint32_t interaction_serial;
    esp_timer_handle_t idle_timer;
    esp_timer_handle_t daypart_timer;
    esp_timer_handle_t schedule_timer;
    esp_timer_handle_t antiburn_timer;
    esp_timer_handle_t fade_timer;
    lv_timer_t *snow_timer;
    lv_obj_t *snow_overlay;
    lv_obj_t *snow_canvas;
    uint16_t *snow_buf;
    size_t snow_buf_pixels;
    bool snow_running;
    bool remote_sleep_armed;
    bool fade_active;
    int current_brightness_percent;
    int fade_start_percent;
    int fade_target_percent;
    int fade_step_count;
    int fade_steps_elapsed;
    char fade_reason[16];
} backlight_state_t;

static const char *TAG = "backlight";
static backlight_state_t s_state;

static void idle_timer_cb(void *arg);
static void daypart_timer_cb(void *arg);
static void schedule_timer_cb(void *arg);
static void antiburn_timer_cb(void *arg);
static void snow_timer_cb(lv_timer_t *timer);
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
static void snow_fill_area(const lv_area_t *area, int hor_res);

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
        ESP_LOGI(TAG, "[antiburn] Interaction detected, stopping pixel training");
        backlight_manager_set_antiburn(false, false);
        consumed = true;
    }

    if (s_state.idle_sleep_active) {
        exit_idle_state("interaction");
        consumed = true;
    }

    poke_lvgl_activity("interaction");
    s_state.interaction_serial++;
    s_state.remote_sleep_armed = false;
    schedule_idle_timer();
    return consumed;
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
        s_state.remote_sleep_armed = false;
        char ts[16] = {0};
        ESP_LOGI(TAG, "[antiburn] Starting pixel training (%s) @ %s",
                 manual ? "manual" : "scheduled",
                 format_now(ts, sizeof(ts)));
        esp_timer_stop(s_state.idle_timer);
        apply_current_brightness("antiburn-start");
        if (!snow_overlay_start()) {
            ESP_LOGW(TAG, "[antiburn] snow overlay failed to start");
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

static void snow_timer_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);
    snow_draw_frame();
}

static void schedule_idle_timer(void)
{
    if (!s_state.initialized) {
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
    if (s_state.idle_sleep_active || s_state.antiburn_active) {
        return;
    }
    s_state.idle_sleep_active = true;
    ESP_LOGI(TAG, "[idle] timeout reached; turning off backlight");
    s_state.remote_sleep_armed = false;
    stop_backlight_fade();
    if (s_state.backlight_lit) {
        esp_err_t err = bsp_display_backlight_off();
        if (err == ESP_OK) {
            s_state.backlight_lit = false;
            s_state.current_brightness_percent = 0;
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
    stop_backlight_fade();

    if (percent <= 0) {
        if (s_state.backlight_lit) {
            esp_err_t err = bsp_display_backlight_off();
            if (err == ESP_OK) {
                s_state.backlight_lit = false;
                s_state.current_brightness_percent = 0;
            } else {
                ESP_LOGW(TAG, "[backlight] disable failed: %s", esp_err_to_name(err));
            }
        }
        return;
    }

    if (s_state.backlight_lit && s_state.current_brightness_percent >= percent) {
        set_brightness_immediate(percent, reason, true);
        return;
    }

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

    if (target_percent <= start_percent) {
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

    int next = start_percent;
    if (delta > 0) {
        next = start_percent + (int)(((int64_t)delta * elapsed) / steps);
        if (next <= s_state.current_brightness_percent) {
            next = s_state.current_brightness_percent + 1;
        }
    } else if (delta < 0) {
        next = start_percent + (int)(((int64_t)delta * elapsed) / steps);
        if (next >= s_state.current_brightness_percent) {
            next = s_state.current_brightness_percent - 1;
        }
    }

    if (next > target_percent) {
        next = target_percent;
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
    const size_t pixels = (size_t)hor * ver;
    if (pixels == 0) {
        return false;
    }
    if (s_state.snow_buf_pixels != pixels || s_state.snow_buf == NULL) {
        if (s_state.snow_buf) {
            heap_caps_free(s_state.snow_buf);
            s_state.snow_buf = NULL;
            s_state.snow_buf_pixels = 0;
        }
        s_state.snow_buf = (uint16_t *)heap_caps_aligned_alloc(SNOW_BUF_ALIGN,
                                                               pixels * sizeof(uint16_t),
                                                               MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (s_state.snow_buf == NULL) {
            ESP_LOGE(TAG, "[snow] buffer alloc failed (%zu bytes)", pixels * sizeof(uint16_t));
            return false;
        }
        s_state.snow_buf_pixels = pixels;
    }

    if (esp_lv_adapter_lock(-1) != ESP_OK) {
        ESP_LOGW(TAG, "[snow] failed to lock LVGL for overlay");
        return false;
    }

    if (s_state.snow_overlay == NULL) {
        s_state.snow_overlay = lv_obj_create(lv_layer_top());
        lv_obj_remove_style_all(s_state.snow_overlay);
        lv_obj_clear_flag(s_state.snow_overlay, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_size(s_state.snow_overlay, hor, ver);
        lv_obj_set_style_bg_color(s_state.snow_overlay, lv_color_hex(0x000000), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(s_state.snow_overlay, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_add_flag(s_state.snow_overlay, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(s_state.snow_overlay, LV_OBJ_FLAG_PRESS_LOCK);
    }

    if (s_state.snow_canvas == NULL) {
        s_state.snow_canvas = lv_canvas_create(s_state.snow_overlay);
        lv_obj_remove_style_all(s_state.snow_canvas);
        lv_obj_set_size(s_state.snow_canvas, hor, ver);
        lv_obj_align(s_state.snow_canvas, LV_ALIGN_CENTER, 0, 0);
    }

    lv_canvas_set_buffer(s_state.snow_canvas, s_state.snow_buf, hor, ver, LV_COLOR_FORMAT_RGB565);
    lv_obj_move_foreground(s_state.snow_overlay);
    memset(s_state.snow_buf, 0, pixels * sizeof(uint16_t));
    lv_obj_invalidate(s_state.snow_canvas);
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
    if (!s_state.snow_running) {
        if (s_state.snow_timer) {
            lv_timer_del(s_state.snow_timer);
            s_state.snow_timer = NULL;
        }
        if (s_state.snow_overlay || s_state.snow_canvas) {
            if (esp_lv_adapter_lock(-1) == ESP_OK) {
                if (s_state.snow_overlay) {
                    lv_obj_del(s_state.snow_overlay);
                    s_state.snow_overlay = NULL;
                    s_state.snow_canvas = NULL;
                }
                esp_lv_adapter_unlock();
            }
        }
        if (s_state.snow_buf) {
            heap_caps_free(s_state.snow_buf);
            s_state.snow_buf = NULL;
            s_state.snow_buf_pixels = 0;
        }
        return;
    }

    if (s_state.snow_timer) {
        lv_timer_del(s_state.snow_timer);
        s_state.snow_timer = NULL;
    }
    s_state.snow_running = false;

    if (esp_lv_adapter_lock(-1) == ESP_OK) {
        if (s_state.snow_overlay) {
            lv_obj_del(s_state.snow_overlay);
            s_state.snow_overlay = NULL;
            s_state.snow_canvas = NULL;
        }
        esp_lv_adapter_unlock();
    }
    if (s_state.snow_buf) {
        heap_caps_free(s_state.snow_buf);
        s_state.snow_buf = NULL;
        s_state.snow_buf_pixels = 0;
    }
    ESP_LOGI(TAG, "[snow] overlay stopped");
}

static void snow_draw_frame(void)
{
    if (!s_state.snow_running || s_state.disp == NULL || s_state.snow_canvas == NULL || s_state.snow_buf == NULL) {
        return;
    }

    const int hor = lv_display_get_horizontal_resolution(s_state.disp);
    const int ver = lv_display_get_vertical_resolution(s_state.disp);
    int iterations = 6 - (int)(lv_display_get_inactive_time(s_state.disp) / 60000);
    if (iterations <= 0) {
        iterations = 1;
    }
    const int rounding = SNOW_MIN_TILE;

    while (iterations-- > 0) {
        int col = (int)(esp_random() % hor);
        col = (col / rounding) * rounding;
        int row = (int)(esp_random() % ver);
        row = (row / rounding) * rounding;
        int size = (int)(esp_random() % 64);
        if (size < rounding) {
            size = rounding;
        }
        size = (size / rounding) * rounding;
        if (size <= 0) {
            size = rounding;
        }

        lv_area_t area = {
            .x1 = col,
            .y1 = row,
            .x2 = col + size,
            .y2 = row + size,
        };
        if (area.x2 >= hor) {
            area.x2 = hor - 1;
        }
        if (area.y2 >= ver) {
            area.y2 = ver - 1;
        }
        snow_fill_area(&area, hor);
    }

    lv_obj_invalidate(s_state.snow_canvas);
}

static void snow_fill_area(const lv_area_t *area, int hor_res)
{
    if (area == NULL || area->x1 > area->x2 || area->y1 > area->y2) {
        return;
    }
    const int width = area->x2 - area->x1 + 1;
    for (int y = area->y1; y <= area->y2; ++y) {
        uint16_t *row = s_state.snow_buf + (y * hor_res) + area->x1;
        for (int x = 0; x < width; ++x) {
            row[x] = (uint16_t)esp_random();
        }
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
    default:
        ESP_LOGW(TAG, "[wake] unknown reason=%d", reason);
        return "unknown";
    }
}
