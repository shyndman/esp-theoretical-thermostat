#include "thermostat/audio_boot.h"

#include <stdbool.h>
#include <stddef.h>

#include "bsp/esp32_p4_nano.h"
#include "esp_check.h"
#include "esp_codec_dev.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "thermostat/application_cues.h"

static const char *TAG = "audio_boot";

extern const uint8_t sound_boot_chime[];
extern const size_t sound_boot_chime_len;
extern const uint8_t sound_failure[];
extern const size_t sound_failure_len;

static esp_codec_dev_handle_t s_codec;
static bool s_prepared;

static esp_err_t ensure_codec_ready(void)
{
  if (s_codec)
  {
    return ESP_OK;
  }

  esp_codec_dev_handle_t handle = bsp_audio_codec_speaker_init();
  if (handle == NULL)
  {
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
  if (rc != ESP_CODEC_DEV_OK)
  {
    ESP_LOGW(TAG, "esp_codec_dev_open failed (%d)", rc);
    esp_codec_dev_delete(handle);
    return ESP_FAIL;
  }

  s_codec = handle;
  return ESP_OK;
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
  int rc = esp_codec_dev_set_out_vol(s_codec, vol);
  if (rc != ESP_CODEC_DEV_OK)
  {
    ESP_LOGW(TAG, "Failed to set speaker volume (%d)", rc);
    return ESP_FAIL;
  }
  ESP_LOGI(TAG, "Speaker volume set to %d%%", vol);
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

  esp_err_t policy = thermostat_application_cues_check(cue_name, CONFIG_THEO_AUDIO_ENABLE);
  if (policy != ESP_OK)
  {
    return policy;
  }

  ESP_RETURN_ON_ERROR(ensure_prepared(), TAG, "Speaker codec not ready");

  int rc = esp_codec_dev_write(s_codec, (void *)buffer, bytes);
  if (rc != ESP_CODEC_DEV_OK)
  {
    ESP_LOGW(TAG, "%s playback failed (%d)", cue_name ? cue_name : "Audio cue", rc);
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "%s playback complete (%u bytes)", cue_name ? cue_name : "Audio cue", (unsigned)bytes);
  return ESP_OK;
}

esp_err_t thermostat_audio_boot_prepare(void)
{
  ESP_RETURN_ON_ERROR(ensure_codec_ready(), TAG, "Speaker codec init failed");
  ESP_RETURN_ON_ERROR(apply_volume(), TAG, "Failed to set speaker volume");
  if (!s_prepared)
  {
    ESP_LOGI(TAG, "Speaker codec prepared for boot audio cues");
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
