#include "streaming/microphone_capture.h"

#include "sdkconfig.h"

#if CONFIG_THEO_MICROPHONE_ENABLE

#include "audio_codec_data_if.h"
#include "driver/i2s_pdm.h"
#include "esp_check.h"
#include "esp_codec_dev_defaults.h"
#include "esp_log.h"
#include "soc/soc_caps.h"

#define TAG "mic_capture"

#ifndef SOC_I2S_SUPPORTS_PDM_RX
#error "SOC_I2S_SUPPORTS_PDM_RX not defined, update SoC caps before enabling microphone"
#endif

static i2s_chan_handle_t s_rx_channel;
static esp_codec_dev_handle_t s_codec_dev;
static const audio_codec_data_if_t *s_data_if;

static void teardown_channel(void)
{
  if (s_rx_channel) {
    i2s_channel_disable(s_rx_channel);
    i2s_del_channel(s_rx_channel);
    s_rx_channel = NULL;
  }
}

esp_err_t microphone_capture_init(void)
{
  if (s_codec_dev) {
    return ESP_OK;
  }

#if !SOC_I2S_SUPPORTS_PDM_RX
  ESP_LOGW(TAG, "PDM RX not supported on this SoC");
  return ESP_ERR_NOT_SUPPORTED;
#else
  i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  chan_cfg.auto_clear = true;
  chan_cfg.dma_desc_num = 4;
  chan_cfg.dma_frame_num = 240;
  ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, NULL, &s_rx_channel), TAG, "i2s_new_channel failed");

  i2s_pdm_rx_config_t pdm_cfg = {
    .clk_cfg = I2S_PDM_RX_CLK_DEFAULT_CONFIG(16000),
    .slot_cfg = I2S_PDM_RX_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
    .gpio_cfg = {
      .clk = CONFIG_THEO_MIC_PDM_CLK_GPIO,
      .din = CONFIG_THEO_MIC_PDM_DATA_GPIO,
      .invert_flags = {
        .clk_inv = false,
      },
    },
  };

  esp_err_t err = i2s_channel_init_pdm_rx_mode(s_rx_channel, &pdm_cfg);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "i2s_channel_init_pdm_rx_mode failed: %s", esp_err_to_name(err));
    teardown_channel();
    return err;
  }

  err = i2s_channel_enable(s_rx_channel);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "i2s_channel_enable failed: %s", esp_err_to_name(err));
    teardown_channel();
    return err;
  }

  audio_codec_i2s_cfg_t i2s_cfg = {
    .port = I2S_NUM_0,
    .rx_handle = s_rx_channel,
    .tx_handle = NULL,
    .clk_src = 0,
  };
  s_data_if = audio_codec_new_i2s_data(&i2s_cfg);
  if (!s_data_if) {
    ESP_LOGE(TAG, "audio_codec_new_i2s_data failed");
    teardown_channel();
    return ESP_ERR_NO_MEM;
  }

  esp_codec_dev_cfg_t dev_cfg = {
    .dev_type = ESP_CODEC_DEV_TYPE_IN,
    .codec_if = NULL,
    .data_if = s_data_if,
  };

  s_codec_dev = esp_codec_dev_new(&dev_cfg);
  if (!s_codec_dev) {
    ESP_LOGE(TAG, "esp_codec_dev_new failed");
    audio_codec_delete_data_if(s_data_if);
    s_data_if = NULL;
    teardown_channel();
    return ESP_ERR_NO_MEM;
  }

  ESP_LOGI(TAG, "Microphone ready (clk=%d din=%d @16kHz)",
           CONFIG_THEO_MIC_PDM_CLK_GPIO,
           CONFIG_THEO_MIC_PDM_DATA_GPIO);
  return ESP_OK;
#endif
}

void microphone_capture_deinit(void)
{
  if (s_codec_dev) {
    esp_codec_dev_close(s_codec_dev);
    esp_codec_dev_delete(s_codec_dev);
    s_codec_dev = NULL;
  }
  if (s_data_if) {
    audio_codec_delete_data_if(s_data_if);
    s_data_if = NULL;
  }
  teardown_channel();
}

esp_codec_dev_handle_t microphone_capture_get_codec(void)
{
  return s_codec_dev;
}

#else

esp_err_t microphone_capture_init(void)
{
  return ESP_OK;
}

void microphone_capture_deinit(void) {}

esp_codec_dev_handle_t microphone_capture_get_codec(void)
{
  return NULL;
}

#endif  // CONFIG_THEO_MICROPHONE_ENABLE
