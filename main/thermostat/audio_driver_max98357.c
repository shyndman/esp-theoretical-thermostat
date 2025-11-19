#include "thermostat/audio_driver.h"

#include "sdkconfig.h"

#include <stdbool.h>

#if CONFIG_THEO_AUDIO_PIPELINE_MAX98357

#include <limits.h>

#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_log.h"

#define PCM_SLICE_BYTES 512
#define I2S_WRITE_TIMEOUT_MS 1000

static const char *TAG = "audio_drv_max";
static i2s_chan_handle_t s_tx_chan;
static bool s_channel_enabled;
static bool s_enable_gpio_configured;
static int s_gain_percent = 100;

static inline int clamp_percent(int raw)
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

static esp_err_t configure_enable_gpio(void)
{
  if (CONFIG_THEO_AUDIO_I2S_ENABLE_GPIO < 0 || s_enable_gpio_configured)
  {
    return ESP_OK;
  }

  gpio_config_t cfg = {
      .pin_bit_mask = 1ULL << CONFIG_THEO_AUDIO_I2S_ENABLE_GPIO,
      .mode = GPIO_MODE_OUTPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };

  ESP_RETURN_ON_ERROR(gpio_config(&cfg), TAG, "Enable GPIO config failed");
  ESP_RETURN_ON_ERROR(gpio_set_level((gpio_num_t)CONFIG_THEO_AUDIO_I2S_ENABLE_GPIO, 1), TAG, "Enable GPIO high failed");
  s_enable_gpio_configured = true;
  return ESP_OK;
}

static esp_err_t ensure_channel_ready(void)
{
  if (s_tx_chan && s_channel_enabled)
  {
    return ESP_OK;
  }

  if (!s_tx_chan)
  {
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    chan_cfg.dma_desc_num = 4;
    chan_cfg.dma_frame_num = 240;

    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &s_tx_chan, NULL), TAG, "i2s_new_channel failed");

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(16000),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = CONFIG_THEO_AUDIO_I2S_BCLK_GPIO,
            .ws = CONFIG_THEO_AUDIO_I2S_LRCLK_GPIO,
            .dout = CONFIG_THEO_AUDIO_I2S_DATA_GPIO,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;
    std_cfg.slot_cfg.ws_width = I2S_BITS_PER_SAMPLE_16BIT;
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_tx_chan, &std_cfg), TAG, "i2s_channel_init_std_mode failed");
  }

  ESP_RETURN_ON_ERROR(configure_enable_gpio(), TAG, "Enable GPIO setup failed");

  if (!s_channel_enabled)
  {
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_tx_chan), TAG, "i2s_channel_enable failed");
    s_channel_enabled = true;
  }

  return ESP_OK;
}

esp_err_t thermostat_audio_driver_init(void)
{
  return ensure_channel_ready();
}

esp_err_t thermostat_audio_driver_set_volume(int percent)
{
  ESP_RETURN_ON_ERROR(ensure_channel_ready(), TAG, "I2S channel not ready");
  s_gain_percent = clamp_percent(percent);
  return ESP_OK;
}

static inline int16_t apply_gain(int16_t sample)
{
  int32_t scaled = ((int32_t)sample * s_gain_percent) / 100;
  if (scaled > INT16_MAX)
  {
    return INT16_MAX;
  }
  if (scaled < INT16_MIN)
  {
    return INT16_MIN;
  }
  return (int16_t)scaled;
}

esp_err_t thermostat_audio_driver_play(const uint8_t *pcm, size_t len)
{
  if (!pcm || len == 0)
  {
    return ESP_ERR_INVALID_ARG;
  }

  if ((len & 1) != 0)
  {
    ESP_LOGW(TAG, "PCM buffer length (%u) must be 16-bit aligned", (unsigned)len);
    return ESP_ERR_INVALID_SIZE;
  }

  ESP_RETURN_ON_ERROR(ensure_channel_ready(), TAG, "I2S channel not ready");

  int16_t scratch[PCM_SLICE_BYTES / sizeof(int16_t)] = {0};
  size_t offset = 0;

  while (offset < len)
  {
    size_t remaining = len - offset;
    size_t chunk_bytes = remaining > PCM_SLICE_BYTES ? PCM_SLICE_BYTES : remaining;
    size_t sample_count = chunk_bytes / sizeof(int16_t);

    for (size_t i = 0; i < sample_count; ++i)
    {
      size_t idx = offset + i * sizeof(int16_t);
      uint16_t lo = pcm[idx];
      uint16_t hi = pcm[idx + 1];
      int16_t raw = (int16_t)((hi << 8) | lo);
      scratch[i] = apply_gain(raw);
    }

    size_t bytes_remaining = sample_count * sizeof(int16_t);
    const uint8_t *chunk_ptr = (const uint8_t *)scratch;
    while (bytes_remaining > 0)
    {
      size_t written = 0;
      esp_err_t err = i2s_channel_write(s_tx_chan, chunk_ptr, bytes_remaining, &written, I2S_WRITE_TIMEOUT_MS);
      if (err != ESP_OK)
      {
        ESP_LOGW(TAG, "i2s write failed (%s)", esp_err_to_name(err));
        return err;
      }
      if (written == 0)
      {
        return ESP_ERR_TIMEOUT;
      }
      bytes_remaining -= written;
      chunk_ptr += written;
    }

    offset += sample_count * sizeof(int16_t);
  }

  return ESP_OK;
}

#endif  // CONFIG_THEO_AUDIO_PIPELINE_MAX98357
