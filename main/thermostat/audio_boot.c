#include "thermostat/audio_boot.h"

#include <stdbool.h>
#include <stddef.h>
#include <time.h>

#include "bsp/esp32_p4_nano.h"
#include "connectivity/time_sync.h"
#include "esp_check.h"
#include "esp_codec_dev.h"
#include "esp_log.h"
#include "sdkconfig.h"

#if CONFIG_THERMO_BOOT_CHIME_ENABLE

static const char *TAG = "audio_boot";

extern const uint8_t sound_boot_chime[];
extern const size_t sound_boot_chime_len;

static esp_codec_dev_handle_t s_codec;

static const uint8_t *get_chime_start(void)
{
    return sound_boot_chime;
}

static size_t get_chime_size(void)
{
    return sound_boot_chime_len;
}

static esp_err_t ensure_codec_ready(void)
{
    if (s_codec) {
        return ESP_OK;
    }

    esp_codec_dev_handle_t handle = bsp_audio_codec_speaker_init();
    if (handle == NULL) {
        ESP_LOGW(TAG, "Speaker codec init failed");
        return ESP_FAIL;
    }

    esp_codec_dev_sample_info_t fs = {
        .sample_rate = 16000,
        .channel = 1,
        .channel_mask = ESP_CODEC_DEV_MAKE_CHANNEL_MASK(0),
        .bits_per_sample = 16,
        .mclk_multiple = 0,
    };

    int rc = esp_codec_dev_open(handle, &fs);
    if (rc != ESP_CODEC_DEV_OK) {
        ESP_LOGW(TAG, "esp_codec_dev_open failed (%d)", rc);
        esp_codec_dev_delete(handle);
        return ESP_FAIL;
    }

    s_codec = handle;
    return ESP_OK;
}

static int clamp_volume_percent(int raw)
{
    if (raw < 0) {
        return 0;
    }
    if (raw > 100) {
        return 100;
    }
    return raw;
}

static esp_err_t apply_volume(void)
{
    int vol = clamp_volume_percent(CONFIG_THERMO_BOOT_CHIME_VOLUME);
    int rc = esp_codec_dev_set_out_vol(s_codec, vol);
    if (rc != ESP_CODEC_DEV_OK) {
        ESP_LOGW(TAG, "Failed to set speaker volume (%d)", rc);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Boot chime volume set to %d%%", vol);
    return ESP_OK;
}

static bool quiet_hours_active(bool *time_known, int *minute_of_day)
{
    const int start = CONFIG_THERMO_BOOT_CHIME_QUIET_START_MINUTE;
    const int end = CONFIG_THERMO_BOOT_CHIME_QUIET_END_MINUTE;

    bool synced = time_sync_wait_for_sync(0);
    if (time_known) {
        *time_known = synced;
    }

    if (!synced) {
        return false;
    }

    if (start == end) {
        return false;
    }

    time_t now = time(NULL);
    struct tm local = {0};
    localtime_r(&now, &local);
    int minute = local.tm_hour * 60 + local.tm_min;
    if (minute_of_day) {
        *minute_of_day = minute;
    }

    if (start < end) {
        return minute >= start && minute < end;
    }

    return minute >= start || minute < end;
}

static void log_quiet_hours_skip(int minute_of_day)
{
    int current_hour = minute_of_day / 60;
    int current_minute = minute_of_day % 60;
    int start_hour = CONFIG_THERMO_BOOT_CHIME_QUIET_START_MINUTE / 60;
    int start_minute = CONFIG_THERMO_BOOT_CHIME_QUIET_START_MINUTE % 60;
    int end_hour = CONFIG_THERMO_BOOT_CHIME_QUIET_END_MINUTE / 60;
    int end_minute = CONFIG_THERMO_BOOT_CHIME_QUIET_END_MINUTE % 60;

    ESP_LOGI(TAG,
             "Boot chime suppressed: quiet hours active (local %02d:%02d within [%02d:%02d,%02d:%02d))",
             current_hour,
             current_minute,
             start_hour,
             start_minute,
             end_hour,
             end_minute);
}

esp_err_t thermostat_audio_boot_try_play(void)
{
    const bool quiet_configured = CONFIG_THERMO_BOOT_CHIME_QUIET_START_MINUTE != CONFIG_THERMO_BOOT_CHIME_QUIET_END_MINUTE;
    bool time_known = !quiet_configured;
    int minute = -1;

    if (quiet_configured && quiet_hours_active(&time_known, &minute)) {
        log_quiet_hours_skip(minute);
        return ESP_OK;
    }

    if (quiet_configured && !time_known) {
        ESP_LOGW(TAG, "Boot chime playing; quiet hours bypassed because clock is unsynchronized");
    }

    ESP_RETURN_ON_ERROR(ensure_codec_ready(), TAG, "Speaker codec not ready");
    ESP_RETURN_ON_ERROR(apply_volume(), TAG, "Failed to set speaker volume");

    size_t bytes = get_chime_size();
    if (bytes == 0) {
        ESP_LOGW(TAG, "Boot chime asset empty; nothing to play");
        return ESP_ERR_INVALID_SIZE;
    }

    if (esp_codec_dev_write(s_codec, (void *)get_chime_start(), bytes) != ESP_CODEC_DEV_OK) {
        ESP_LOGW(TAG, "Boot chime playback failed");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Boot chime playback complete (%u bytes)", (unsigned)bytes);
    return ESP_OK;
}

#else // CONFIG_THERMO_BOOT_CHIME_ENABLE

esp_err_t thermostat_audio_boot_try_play(void)
{
    ESP_LOGI("audio_boot", "Boot chime disabled via CONFIG_THERMO_BOOT_CHIME_ENABLE");
    return ESP_OK;
}

#endif
