#include "thermostat/audio_driver.h"

#include "sdkconfig.h"

#include <stdbool.h>

#if CONFIG_THEO_AUDIO_PIPELINE_MAX98357

#include <limits.h>

#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define PCM_SLICE_BYTES 512
#define I2S_WRITE_TIMEOUT_MS 1000
#define AMP_WARMUP_MS 10
#define AMP_TAIL_MS 100

static const char *TAG = "audio_drv_max";
static i2s_chan_handle_t s_tx_chan;
static bool s_channel_configured;
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

static void amp_off(void)
{
  gpio_set_level(CONFIG_THEO_AUDIO_MAX98357_SDMODE_GPIO, 0);
  ESP_LOGI(TAG, "Amp OFF (SD/MODE LOW)");
}

static esp_err_t amp_on(void)
{
  ESP_RETURN_ON_ERROR(
      gpio_set_level(CONFIG_THEO_AUDIO_MAX98357_SDMODE_GPIO, 1),
      TAG, "SD/MODE HIGH failed");
  ESP_LOGI(TAG, "Amp ON (SD/MODE HIGH), waiting %d ms", AMP_WARMUP_MS);
  vTaskDelay(pdMS_TO_TICKS(AMP_WARMUP_MS));
  return ESP_OK;
}

esp_err_t thermostat_audio_driver_init(void)
{
  if (s_channel_configured)
  {
    return ESP_OK;
  }

  // Configure SD/MODE as output, drive LOW (amp off)
  gpio_config_t io_conf = {
      .pin_bit_mask = (1ULL << CONFIG_THEO_AUDIO_MAX98357_SDMODE_GPIO),
      .mode = GPIO_MODE_OUTPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  ESP_RETURN_ON_ERROR(gpio_config(&io_conf), TAG, "SD/MODE gpio_config failed");
  amp_off();

  // Create I2S channel
  i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  chan_cfg.auto_clear = true;
  chan_cfg.dma_desc_num = 4;
  chan_cfg.dma_frame_num = 240;

  ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &s_tx_chan, NULL), TAG, "i2s_new_channel failed");

  // Configure I2S in standard mode (but don't enable yet)
  i2s_std_config_t std_cfg = {
      .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(16000),
      .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
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

  ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_tx_chan, &std_cfg), TAG, "i2s_channel_init_std_mode failed");

  s_channel_configured = true;
  ESP_LOGI(TAG, "MAX98357 driver initialized (SD/MODE=GPIO%d, idle)", CONFIG_THEO_AUDIO_MAX98357_SDMODE_GPIO);
  return ESP_OK;
}

esp_err_t thermostat_audio_driver_set_volume(int percent)
{
  if (!s_channel_configured)
  {
    ESP_RETURN_ON_ERROR(thermostat_audio_driver_init(), TAG, "Driver not initialized");
  }
  s_gain_percent = clamp_percent(percent);
  ESP_LOGI(TAG, "Gain set to %d%% (raw input: %d)", s_gain_percent, percent);
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

  if (!s_channel_configured)
  {
    ESP_RETURN_ON_ERROR(thermostat_audio_driver_init(), TAG, "Driver not initialized");
  }

  ESP_LOGI(TAG, "Playing %u bytes with gain %d%%", (unsigned)len, s_gain_percent);

  // Enable amp: SD/MODE HIGH + warmup delay
  esp_err_t err = amp_on();
  if (err != ESP_OK)
  {
    return err;
  }

  // Enable I2S TX
  err = i2s_channel_enable(s_tx_chan);
  if (err != ESP_OK)
  {
    ESP_LOGE(TAG, "i2s_channel_enable failed: %s", esp_err_to_name(err));
    amp_off();
    return err;
  }

  // Stereo output buffer: each mono sample becomes L+R pair
  int16_t scratch[PCM_SLICE_BYTES / sizeof(int16_t)] = {0};
  size_t offset = 0;

  // Max mono input bytes per iteration (each mono sample expands to stereo)
  const size_t max_mono_chunk = PCM_SLICE_BYTES / 2;
  esp_err_t write_err = ESP_OK;

  while (offset < len)
  {
    size_t remaining = len - offset;
    size_t chunk_bytes = remaining > max_mono_chunk ? max_mono_chunk : remaining;
    size_t mono_sample_count = chunk_bytes / sizeof(int16_t);

    // Convert mono to stereo: duplicate each sample to L and R channels
    for (size_t i = 0; i < mono_sample_count; ++i)
    {
      size_t idx = offset + i * sizeof(int16_t);
      uint16_t lo = pcm[idx];
      uint16_t hi = pcm[idx + 1];
      int16_t raw = (int16_t)((hi << 8) | lo);
      int16_t sample = apply_gain(raw);
      scratch[i * 2] = sample;      // Left channel
      scratch[i * 2 + 1] = sample;  // Right channel
    }

    size_t bytes_remaining = mono_sample_count * 2 * sizeof(int16_t);
    const uint8_t *chunk_ptr = (const uint8_t *)scratch;
    while (bytes_remaining > 0)
    {
      size_t written = 0;
      write_err = i2s_channel_write(s_tx_chan, chunk_ptr, bytes_remaining, &written, I2S_WRITE_TIMEOUT_MS);
      if (write_err != ESP_OK)
      {
        ESP_LOGW(TAG, "i2s write failed (%s)", esp_err_to_name(write_err));
        goto cleanup;
      }
      if (written == 0)
      {
        write_err = ESP_ERR_TIMEOUT;
        goto cleanup;
      }
      bytes_remaining -= written;
      chunk_ptr += written;
    }

    offset += mono_sample_count * sizeof(int16_t);
  }

cleanup:
  // Tail delay to let DMA drain
  vTaskDelay(pdMS_TO_TICKS(AMP_TAIL_MS));

  // Disable I2S TX
  esp_err_t disable_err = i2s_channel_disable(s_tx_chan);
  if (disable_err != ESP_OK)
  {
    ESP_LOGW(TAG, "i2s_channel_disable failed: %s", esp_err_to_name(disable_err));
  }

  // Disable amp: SD/MODE LOW
  amp_off();

  return write_err;
}

#endif  // CONFIG_THEO_AUDIO_PIPELINE_MAX98357
