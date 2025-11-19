#include "thermostat/audio_boot.h"

#include <stdbool.h>
#include <stddef.h>
#include <time.h>

#include "connectivity/time_sync.h"
#include "esp_check.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "thermostat/audio_driver.h"

static const char *TAG = "audio_boot";

#if CONFIG_THEO_AUDIO_ENABLE

extern const uint8_t sound_boot_chime[];
extern const size_t sound_boot_chime_len;
extern const uint8_t sound_failure[];
extern const size_t sound_failure_len;

static bool s_prepared;

static esp_err_t ensure_driver_ready(void)
{
  esp_err_t err = thermostat_audio_driver_init();
  if (err != ESP_OK)
  {
    ESP_LOGW(TAG, "Audio driver init failed (%s)", esp_err_to_name(err));
  }
  return err;
}

static int clamp_volume_percent(int raw)
{
  if (raw < 0)
  {
    return 0;
  }
  if (raw > 100)
  {
    return 100;
  }
  return raw;
}

static esp_err_t apply_volume(void)
{
  int vol = clamp_volume_percent(CONFIG_THEO_BOOT_CHIME_VOLUME);
  esp_err_t err = thermostat_audio_driver_set_volume(vol);
  if (err != ESP_OK)
  {
    ESP_LOGW(TAG, "Failed to set speaker volume (%s)", esp_err_to_name(err));
    return err;
  }
  ESP_LOGI(TAG, "Speaker volume set to %d%%", vol);
  return ESP_OK;
}

static bool quiet_window_configured(void)
{
  return CONFIG_THEO_BOOT_CHIME_QUIET_START_MINUTE != CONFIG_THEO_BOOT_CHIME_QUIET_END_MINUTE;
}

static bool quiet_hours_active(int *minute_of_day)
{
  if (!quiet_window_configured())
  {
    return false;
  }

  time_t now = time(NULL);
  struct tm local = {0};
  localtime_r(&now, &local);
  int minute = local.tm_hour * 60 + local.tm_min;
  if (minute_of_day)
  {
    *minute_of_day = minute;
  }

  const int start = CONFIG_THEO_BOOT_CHIME_QUIET_START_MINUTE;
  const int end = CONFIG_THEO_BOOT_CHIME_QUIET_END_MINUTE;
  if (start == end)
  {
    return false;
  }

  if (start < end)
  {
    return minute >= start && minute < end;
  }
  return minute >= start || minute < end;
}

static void log_quiet_hours_skip(const char *cue_name, int minute_of_day)
{
  int current_hour = minute_of_day / 60;
  int current_minute = minute_of_day % 60;
  int start_hour = CONFIG_THEO_BOOT_CHIME_QUIET_START_MINUTE / 60;
  int start_minute = CONFIG_THEO_BOOT_CHIME_QUIET_START_MINUTE % 60;
  int end_hour = CONFIG_THEO_BOOT_CHIME_QUIET_END_MINUTE / 60;
  int end_minute = CONFIG_THEO_BOOT_CHIME_QUIET_END_MINUTE % 60;

  ESP_LOGW(TAG,
           "%s suppressed: quiet hours active (local %02d:%02d within [%02d:%02d,%02d:%02d))",
           cue_name,
           current_hour,
           current_minute,
           start_hour,
           start_minute,
           end_hour,
           end_minute);
}

static esp_err_t audio_policy_check(const char *cue_name)
{
  if (!cue_name)
  {
    cue_name = "Audio cue";
  }

  if (!time_sync_wait_for_sync(0))
  {
    ESP_LOGW(TAG, "%s suppressed: clock unsynchronized", cue_name);
    return ESP_ERR_INVALID_STATE;
  }

  int minute = -1;
  if (quiet_hours_active(&minute))
  {
    log_quiet_hours_skip(cue_name, minute);
    return ESP_ERR_INVALID_STATE;
  }

  return ESP_OK;
}

static esp_err_t ensure_prepared(void)
{
  if (s_prepared)
  {
    return ESP_OK;
  }
  return thermostat_audio_boot_prepare();
}

static esp_err_t play_pcm_buffer(const char *cue_name, const uint8_t *buffer, size_t bytes)
{
  if (!buffer || bytes == 0)
  {
    ESP_LOGW(TAG, "%s asset invalid", cue_name ? cue_name : "Audio cue");
    return ESP_ERR_INVALID_SIZE;
  }

  esp_err_t policy = audio_policy_check(cue_name);
  if (policy != ESP_OK)
  {
    return policy;
  }

  ESP_RETURN_ON_ERROR(ensure_prepared(), TAG, "Audio pipeline not ready");
  esp_err_t err = thermostat_audio_driver_play(buffer, bytes);
  if (err != ESP_OK)
  {
    ESP_LOGW(TAG, "%s playback failed (%s)", cue_name ? cue_name : "Audio cue", esp_err_to_name(err));
    return err;
  }

  ESP_LOGI(TAG, "%s playback complete (%u bytes)", cue_name ? cue_name : "Audio cue", (unsigned)bytes);
  return ESP_OK;
}

esp_err_t thermostat_audio_boot_prepare(void)
{
  ESP_RETURN_ON_ERROR(ensure_driver_ready(), TAG, "Audio driver init failed");
  ESP_RETURN_ON_ERROR(apply_volume(), TAG, "Failed to set speaker volume");
  if (!s_prepared)
  {
    ESP_LOGI(TAG, "Audio pipeline prepared for boot cues");
  }
  s_prepared = true;
  return ESP_OK;
}

esp_err_t thermostat_audio_boot_try_play(void)
{
#if !CONFIG_THEO_BOOT_CHIME_ENABLE
  ESP_LOGI(TAG, "Boot chime disabled via CONFIG_THEO_BOOT_CHIME_ENABLE");
  return ESP_OK;
#else
  return play_pcm_buffer("Boot chime", sound_boot_chime, sound_boot_chime_len);
#endif
}

esp_err_t thermostat_audio_boot_play_failure(void)
{
  return play_pcm_buffer("Failure tone", sound_failure, sound_failure_len);
}

#else  // CONFIG_THEO_AUDIO_ENABLE

esp_err_t thermostat_audio_boot_prepare(void)
{
  ESP_LOGI(TAG, "Application audio disabled; speaker prep skipped");
  return ESP_ERR_INVALID_STATE;
}

esp_err_t thermostat_audio_boot_try_play(void)
{
  ESP_LOGI(TAG, "Boot chime suppressed: application audio disabled");
  return ESP_ERR_INVALID_STATE;
}

esp_err_t thermostat_audio_boot_play_failure(void)
{
  ESP_LOGI(TAG, "Failure tone suppressed: application audio disabled");
  return ESP_ERR_INVALID_STATE;
}

#endif  // CONFIG_THEO_AUDIO_ENABLE
