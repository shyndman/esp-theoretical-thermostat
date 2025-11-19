#include "thermostat/audio_driver.h"

#include "sdkconfig.h"

#if CONFIG_THEO_AUDIO_PIPELINE_NANO_BSP

#include "bsp/esp32_p4_nano.h"
#include "esp_check.h"
#include "esp_codec_dev.h"
#include "esp_log.h"

static const char *TAG = "audio_drv_bsp";
static esp_codec_dev_handle_t s_codec;

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

esp_err_t thermostat_audio_driver_init(void)
{
  return ensure_codec_ready();
}

esp_err_t thermostat_audio_driver_set_volume(int percent)
{
  ESP_RETURN_ON_ERROR(ensure_codec_ready(), TAG, "Codec not ready");
  int rc = esp_codec_dev_set_out_vol(s_codec, percent);
  if (rc != ESP_CODEC_DEV_OK)
  {
    ESP_LOGW(TAG, "esp_codec_dev_set_out_vol failed (%d)", rc);
    return ESP_FAIL;
  }
  return ESP_OK;
}

esp_err_t thermostat_audio_driver_play(const uint8_t *pcm, size_t len)
{
  if (!pcm || len == 0)
  {
    return ESP_ERR_INVALID_ARG;
  }

  ESP_RETURN_ON_ERROR(ensure_codec_ready(), TAG, "Codec not ready");
  int rc = esp_codec_dev_write(s_codec, (void *)pcm, len);
  if (rc != ESP_CODEC_DEV_OK)
  {
    ESP_LOGW(TAG, "esp_codec_dev_write failed (%d)", rc);
    return ESP_FAIL;
  }
  return ESP_OK;
}

#endif  // CONFIG_THEO_AUDIO_PIPELINE_NANO_BSP
