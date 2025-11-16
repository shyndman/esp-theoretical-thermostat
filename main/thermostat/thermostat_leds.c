#include "thermostat/thermostat_leds.h"

#include <math.h>
#include <stdio.h>

#include "connectivity/time_sync.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "led_strip.h"
#include "sdkconfig.h"
#include "thermostat/application_cues.h"

#ifndef LED_PI
#define LED_PI 3.14159265f
#endif

#define THERMOSTAT_LED_COUNT        (6)
#define LED_TIMER_PERIOD_US         (10000)  // 10 ms tick
#define LED_MIN_FADE_DURATION_MS    (100)

typedef enum {
  LED_EFFECT_IDLE = 0,
  LED_EFFECT_PULSE,
  LED_EFFECT_FADE,
} led_effect_type_t;

typedef struct {
  thermostat_led_color_t color;
  float hz;
  int64_t start_time_us;
} pulse_state_t;

typedef struct {
  thermostat_led_color_t color;
  float start_brightness;
  float target_brightness;
  int64_t start_time_us;
  int64_t duration_us;
} fade_state_t;

typedef struct {
  led_strip_handle_t strip;
  esp_timer_handle_t timer;
  led_effect_type_t effect;
  pulse_state_t pulse;
  fade_state_t fade;
  thermostat_led_color_t latched_color;
  float latched_brightness;
  bool initialized;
  bool available;
  bool quiet_gate_active;
} thermostat_led_runtime_t;

static thermostat_led_runtime_t s_leds = {0};
static const char *TAG = "thermo_leds";

static void led_effect_timer(void *arg);
static esp_err_t write_fill(thermostat_led_color_t color, float brightness);
static void cancel_current_effect(void);
static bool cue_gate_required(void);
static esp_err_t guard_output(const char *cue_name);

static float clamp_unit(float value)
{
  if (value < 0.0f)
  {
    return 0.0f;
  }
  if (value > 1.0f)
  {
    return 1.0f;
  }
  return value;
}

static void ensure_timer_created(void)
{
  if (s_leds.timer)
  {
    return;
  }

  const esp_timer_create_args_t args = {
      .callback = led_effect_timer,
      .name = "led_effect",
  };
  if (esp_timer_create(&args, &s_leds.timer) != ESP_OK)
  {
    ESP_LOGE(TAG, "Failed to create LED effect timer");
  }
}

static void stop_timer(void)
{
  if (s_leds.timer)
  {
    esp_timer_stop(s_leds.timer);
  }
}

static void start_timer(void)
{
  ensure_timer_created();
  if (!s_leds.timer)
  {
    return;
  }
  esp_err_t err = esp_timer_start_periodic(s_leds.timer, LED_TIMER_PERIOD_US);
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
  {
    ESP_LOGW(TAG, "Failed to start LED timer (%s)", esp_err_to_name(err));
  }
}

static void cancel_current_effect(void)
{
  stop_timer();
  s_leds.effect = LED_EFFECT_IDLE;
}

static bool cue_gate_required(void)
{
  if (!s_leds.quiet_gate_active && time_sync_wait_for_sync(0))
  {
    s_leds.quiet_gate_active = true;
    ESP_LOGI(TAG, "SNTP synchronized; quiet-hours gate now enforced");
  }
  return s_leds.quiet_gate_active;
}

static void format_cue_desc(char *buffer, size_t len, const char *label, thermostat_led_color_t color)
{
  if (!buffer || len == 0)
  {
    return;
  }
  snprintf(buffer, len, "%s #%02x%02x%02x", label, color.r, color.g, color.b);
}

static esp_err_t guard_output(const char *cue_name)
{
  if (!s_leds.available)
  {
    return ESP_ERR_INVALID_STATE;
  }

  if (!cue_gate_required())
  {
    return ESP_OK;
  }

  return thermostat_application_cues_check(cue_name, CONFIG_THEO_LED_ENABLE);
}

static esp_err_t write_fill(thermostat_led_color_t color, float brightness)
{
  if (!s_leds.available)
  {
    return ESP_ERR_INVALID_STATE;
  }

  brightness = clamp_unit(brightness);
  uint32_t r = (uint32_t)lroundf((float)color.r * brightness);
  uint32_t g = (uint32_t)lroundf((float)color.g * brightness);
  uint32_t b = (uint32_t)lroundf((float)color.b * brightness);

  for (int i = 0; i < THERMOSTAT_LED_COUNT; ++i)
  {
    esp_err_t pixel_err = led_strip_set_pixel_rgbw(s_leds.strip, i, r, g, b, 0);
    if (pixel_err != ESP_OK)
    {
      return pixel_err;
    }
  }
  esp_err_t err = led_strip_refresh(s_leds.strip);
  if (err == ESP_OK)
  {
    s_leds.latched_color = color;
    s_leds.latched_brightness = brightness;
  }
  else
  {
    ESP_LOGW(TAG, "LED refresh failed (%s)", esp_err_to_name(err));
  }
  return err;
}

esp_err_t thermostat_leds_init(void)
{
#if !CONFIG_THEO_LED_ENABLE
  ESP_LOGI(TAG, "LED notifications disabled via CONFIG_THEO_LED_ENABLE");
  s_leds.available = false;
  return ESP_ERR_NOT_SUPPORTED;
#else
  if (s_leds.initialized)
  {
    return ESP_OK;
  }

  led_strip_config_t strip_config = {
      .strip_gpio_num = CONFIG_THEO_LED_STRIP_GPIO,
      .max_leds = THERMOSTAT_LED_COUNT,
      .led_model = LED_MODEL_WS2812,
      .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRBW,
      .flags = {.invert_out = 0},
  };
  led_strip_rmt_config_t rmt_config = {
      .clk_src = RMT_CLK_SRC_DEFAULT,
      .resolution_hz = 10 * 1000 * 1000,
      .mem_block_symbols = 0,
      .flags = {.with_dma = 0},
  };

  esp_err_t err = led_strip_new_rmt_device(&strip_config, &rmt_config, &s_leds.strip);
  if (err != ESP_OK)
  {
    ESP_LOGE(TAG, "LED strip init failed (%s)", esp_err_to_name(err));
    return err;
  }
  err = led_strip_clear(s_leds.strip);
  if (err != ESP_OK)
  {
    ESP_LOGE(TAG, "Failed to clear strip (%s)", esp_err_to_name(err));
    goto cleanup;
  }
  err = led_strip_refresh(s_leds.strip);
  if (err != ESP_OK)
  {
    ESP_LOGE(TAG, "Failed to refresh strip (%s)", esp_err_to_name(err));
    goto cleanup;
  }

  ensure_timer_created();
  if (!s_leds.timer)
  {
    err = ESP_FAIL;
    goto cleanup;
  }

  s_leds.initialized = true;
  s_leds.available = true;
  s_leds.effect = LED_EFFECT_IDLE;
  s_leds.latched_color = thermostat_led_color(0, 0, 0);
  s_leds.latched_brightness = 0.0f;
  s_leds.quiet_gate_active = false;

  ESP_LOGI(TAG, "LED strip ready on GPIO %d", CONFIG_THEO_LED_STRIP_GPIO);
  return ESP_OK;

cleanup:
  if (s_leds.strip)
  {
    led_strip_del(s_leds.strip);
    s_leds.strip = NULL;
  }
  return err;
#endif
}

bool thermostat_leds_available(void)
{
  return s_leds.available;
}

void thermostat_leds_notify_boot_complete(void)
{
  if (s_leds.available)
  {
    s_leds.quiet_gate_active = true;
    ESP_LOGI(TAG, "Boot stage done; LED cues now quiet-hours gated");
  }
}

static esp_err_t start_fade(thermostat_led_color_t color, float start_brightness, float target_brightness, uint32_t fade_ms)
{
  if (!s_leds.available)
  {
    return ESP_ERR_INVALID_STATE;
  }

  if (fade_ms < LED_MIN_FADE_DURATION_MS)
  {
    fade_ms = LED_MIN_FADE_DURATION_MS;
  }

  cancel_current_effect();
  s_leds.effect = LED_EFFECT_FADE;
  s_leds.fade.color = color;
  s_leds.fade.start_brightness = clamp_unit(start_brightness);
  s_leds.fade.target_brightness = clamp_unit(target_brightness);
  s_leds.fade.start_time_us = esp_timer_get_time();
  s_leds.fade.duration_us = (int64_t)fade_ms * 1000;

  write_fill(color, s_leds.fade.start_brightness);
  start_timer();
  return ESP_OK;
}

esp_err_t thermostat_leds_solid_with_fade(thermostat_led_color_t color, uint32_t fade_ms)
{
  if (!s_leds.available)
  {
    return ESP_ERR_INVALID_STATE;
  }

  char cue[32];
  format_cue_desc(cue, sizeof(cue), "LED fade", color);
  esp_err_t err = guard_output(cue);
  if (err != ESP_OK)
  {
    return err;
  }

  ESP_LOGD(TAG, "LED fade to #%02x%02x%02x over %ums", color.r, color.g, color.b, (unsigned)fade_ms);
  return start_fade(color, 0.0f, 1.0f, fade_ms);
}

esp_err_t thermostat_leds_off_with_fade(uint32_t fade_ms)
{
  if (!s_leds.available)
  {
    return ESP_ERR_INVALID_STATE;
  }

  esp_err_t err = guard_output("LED fade off");
  if (err != ESP_OK)
  {
    return err;
  }

  ESP_LOGD(TAG, "LED fade to black over %ums", (unsigned)fade_ms);
  return start_fade(s_leds.latched_color, s_leds.latched_brightness, 0.0f, fade_ms);
}

esp_err_t thermostat_leds_pulse(thermostat_led_color_t color, float hz)
{
  if (!s_leds.available)
  {
    return ESP_ERR_INVALID_STATE;
  }
  if (hz <= 0.0f)
  {
    return ESP_ERR_INVALID_ARG;
  }

  char cue[32];
  format_cue_desc(cue, sizeof(cue), "LED pulse", color);
  esp_err_t err = guard_output(cue);
  if (err != ESP_OK)
  {
    return err;
  }

  ESP_LOGD(TAG, "LED pulse #%02x%02x%02x @ %.2f Hz", color.r, color.g, color.b, hz);
  cancel_current_effect();
  s_leds.effect = LED_EFFECT_PULSE;
  s_leds.pulse.color = color;
  s_leds.pulse.hz = hz;
  s_leds.pulse.start_time_us = esp_timer_get_time();
  start_timer();
  return ESP_OK;
}

static void complete_fade_if_done(int64_t now)
{
  if (s_leds.effect != LED_EFFECT_FADE)
  {
    return;
  }

  int64_t elapsed = now - s_leds.fade.start_time_us;
  if (elapsed < 0)
  {
    elapsed = 0;
  }

  float progress = (float)elapsed / (float)s_leds.fade.duration_us;
  if (progress > 1.0f)
  {
    progress = 1.0f;
  }

  float brightness = s_leds.fade.start_brightness + (s_leds.fade.target_brightness - s_leds.fade.start_brightness) * progress;
  write_fill(s_leds.fade.color, brightness);

  if (progress >= 1.0f)
  {
    if (s_leds.fade.target_brightness == 0.0f)
    {
      s_leds.latched_color = thermostat_led_color(0, 0, 0);
    }
    cancel_current_effect();
  }
}

static void update_pulse(int64_t now)
{
  if (s_leds.effect != LED_EFFECT_PULSE)
  {
    return;
  }

  float elapsed_s = (float)(now - s_leds.pulse.start_time_us) / 1000000.0f;
  if (elapsed_s < 0.0f)
  {
    elapsed_s = 0.0f;
  }

  float period = 1.0f / s_leds.pulse.hz;
  if (period <= 0.0f)
  {
    period = 1.0f;
  }

  float phase = fmodf(elapsed_s, period) / period;
  float brightness = 0.5f - 0.5f * cosf(phase * 2.0f * LED_PI);
  write_fill(s_leds.pulse.color, brightness);
}

static void led_effect_timer(void *arg)
{
  int64_t now = esp_timer_get_time();
  if (s_leds.effect == LED_EFFECT_FADE)
  {
    complete_fade_if_done(now);
  }
  else if (s_leds.effect == LED_EFFECT_PULSE)
  {
    update_pulse(now);
  }

  if (s_leds.effect == LED_EFFECT_IDLE)
  {
    stop_timer();
  }
}
