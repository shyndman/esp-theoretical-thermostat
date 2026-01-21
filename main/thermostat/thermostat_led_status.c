#include "thermostat/thermostat_led_status.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "thermostat/thermostat_leds.h"
#include "thermostat/audio_boot.h"
#include "thermostat/ui_animation_timing.h"
#include "thermostat/ui_splash.h"

#define LED_STATUS_SPARKLE_POLL_MS  (20)
#define LED_STATUS_RAINBOW_TIMEOUT_MS (10000)

static const char *TAG = "led_status";

typedef enum {
  TIMER_STAGE_NONE = 0,
  TIMER_STAGE_BOOT_WAIT_SPARKLE,
  TIMER_STAGE_BOOT_WHITE_PEAK,
  TIMER_STAGE_BOOT_HOLD,
  TIMER_STAGE_BOOT_COMPLETE,
  TIMER_STAGE_TIMED_EFFECT_TIMEOUT,
} timer_stage_t;

static struct {
  bool leds_ready;
  bool booting;
  bool boot_sequence_active;
  bool timed_effect_active;
  bool heating;
  bool cooling;
  bool screen_on;
  bool bias_lighting_active;
  esp_timer_handle_t timer;
  timer_stage_t timer_stage;
} s_status = {
    .booting = true,
};

static void led_status_timer_cb(void *arg);
static void apply_hvac_effect(void);
static void schedule_timer(timer_stage_t stage, uint32_t delay_ms);
static void log_if_error(esp_err_t err, const char *stage);
static void start_boot_success_sequence(void);
static void try_play_boot_chime(void);
static void boot_chime_task(void *arg);
static void start_bias_lighting(void);

esp_err_t thermostat_led_status_init(void)
{
  esp_err_t err = thermostat_leds_init();
  if (err != ESP_OK)
  {
    ESP_LOGW(TAG, "LED service unavailable (%s)", esp_err_to_name(err));
    s_status.leds_ready = false;
    return err;
  }

  s_status.leds_ready = thermostat_leds_available();
  if (!s_status.leds_ready)
  {
    return ESP_ERR_INVALID_STATE;
  }

  const esp_timer_create_args_t args = {
      .callback = led_status_timer_cb,
      .name = "led_status",
  };
  if (esp_timer_create(&args, &s_status.timer) != ESP_OK)
  {
    ESP_LOGW(TAG, "Failed to create LED status timer");
  }

  return ESP_OK;
}

void thermostat_led_status_booting(void)
{
  if (!s_status.leds_ready)
  {
    return;
  }

  s_status.booting = true;
  ESP_LOGI(TAG, "Boot sparkle engaged");
  log_if_error(thermostat_leds_start_sparkle(), "boot sparkle");
}

static void schedule_timer(timer_stage_t stage, uint32_t delay_ms)
{
  if (!s_status.timer)
  {
    return;
  }
  esp_timer_stop(s_status.timer);
  s_status.timer_stage = stage;
  esp_err_t err = esp_timer_start_once(s_status.timer, (uint64_t)delay_ms * 1000ULL);
  if (err != ESP_OK)
  {
    ESP_LOGW(TAG, "LED status timer start failed (%s)", esp_err_to_name(err));
  }
}

void thermostat_led_status_boot_complete(void)
{
  if (!s_status.leds_ready)
  {
    thermostat_splash_begin_fade();
    return;
  }

  s_status.booting = false;
  thermostat_leds_stop_animation();
  if (!s_status.timer)
  {
    ESP_LOGW(TAG, "LED status timer unavailable; cannot wait for sparkle drain");
    start_boot_success_sequence();
    return;
  }

  if (thermostat_leds_is_animating())
  {
    s_status.boot_sequence_active = true;
    ESP_LOGI(TAG, "Boot complete; waiting for sparkle drain before success fade");
    schedule_timer(TIMER_STAGE_BOOT_WAIT_SPARKLE, LED_STATUS_SPARKLE_POLL_MS);
    return;
  }

  start_boot_success_sequence();
}

void thermostat_led_status_set_hvac(bool heating, bool cooling)
{
  if (!s_status.leds_ready)
  {
    return;
  }

  s_status.heating = heating;
  s_status.cooling = cooling;
  ESP_LOGD(TAG, "HVAC LED update heating=%d cooling=%d", s_status.heating, s_status.cooling);
  apply_hvac_effect();
}

void thermostat_led_status_trigger_rainbow(void)
{
  if (!s_status.leds_ready)
  {
    return;
  }

  ESP_LOGI(TAG, "Rainbow easter egg triggered");
  s_status.timed_effect_active = true;
  s_status.bias_lighting_active = false;
  log_if_error(thermostat_leds_rainbow(), "rainbow trigger");
  schedule_timer(TIMER_STAGE_TIMED_EFFECT_TIMEOUT, LED_STATUS_RAINBOW_TIMEOUT_MS);
}

void thermostat_led_status_trigger_heatwave(void)
{
  if (!s_status.leds_ready)
  {
    return;
  }

  ESP_LOGI(TAG, "Heatwave easter egg triggered");
  s_status.timed_effect_active = true;
  s_status.bias_lighting_active = false;
  log_if_error(thermostat_leds_wave_rising(thermostat_led_color(0xA0, 0x38, 0x05)), "heatwave trigger");
  schedule_timer(TIMER_STAGE_TIMED_EFFECT_TIMEOUT, LED_STATUS_RAINBOW_TIMEOUT_MS);
}

void thermostat_led_status_trigger_coolwave(void)
{
  if (!s_status.leds_ready)
  {
    return;
  }

  ESP_LOGI(TAG, "Coolwave easter egg triggered");
  s_status.timed_effect_active = true;
  s_status.bias_lighting_active = false;
  log_if_error(thermostat_leds_wave_falling(thermostat_led_color(0x20, 0x65, 0xB0)), "coolwave trigger");
  schedule_timer(TIMER_STAGE_TIMED_EFFECT_TIMEOUT, LED_STATUS_RAINBOW_TIMEOUT_MS);
}

void thermostat_led_status_trigger_sparkle(void)
{
  if (!s_status.leds_ready)
  {
    return;
  }

  ESP_LOGI(TAG, "Sparkle easter egg triggered");
  s_status.timed_effect_active = true;
  s_status.bias_lighting_active = false;
  log_if_error(thermostat_leds_start_sparkle(), "sparkle trigger");
  schedule_timer(TIMER_STAGE_TIMED_EFFECT_TIMEOUT, LED_STATUS_RAINBOW_TIMEOUT_MS);
}

static void apply_hvac_effect(void)
{
  if (!s_status.leds_ready || s_status.booting || s_status.boot_sequence_active ||
      s_status.timed_effect_active)
  {
    return;
  }

  if (s_status.heating)
  {
    // Deep orange #A03805 base, rising wave (heat rises)
    s_status.bias_lighting_active = false;
    log_if_error(thermostat_leds_wave_rising(thermostat_led_color(0xA0, 0x38, 0x05)),
                 "heating wave");
  }
  else if (s_status.cooling)
  {
    // Saturated blue #2065B0 base, falling wave (cold sinks)
    s_status.bias_lighting_active = false;
    log_if_error(thermostat_leds_wave_falling(thermostat_led_color(0x20, 0x65, 0xB0)),
                 "cooling wave");
  }
  else if (s_status.screen_on)
  {
    // No HVAC demand, screen is on - restore bias lighting
    ESP_LOGD(TAG, "HVAC idle, screen on: restoring bias lighting");
    start_bias_lighting();
  }
  else
  {
    log_if_error(thermostat_leds_off_with_fade(100), "idle fade-off");
  }
}

static void led_status_timer_cb(void *arg)
{
  switch (s_status.timer_stage)
  {
    case TIMER_STAGE_BOOT_WAIT_SPARKLE:
      if (thermostat_leds_is_animating())
      {
        schedule_timer(TIMER_STAGE_BOOT_WAIT_SPARKLE, LED_STATUS_SPARKLE_POLL_MS);
      }
      else
      {
        ESP_LOGI(TAG, "Sparkle drained; starting boot success sequence");
        start_boot_success_sequence();
      }
      break;
    case TIMER_STAGE_BOOT_WHITE_PEAK:
      schedule_timer(TIMER_STAGE_BOOT_HOLD, THERMOSTAT_ANIM_LED_WHITE_HOLD_MS);
      try_play_boot_chime();
      break;
    case TIMER_STAGE_BOOT_HOLD:
      thermostat_splash_begin_fade();
      log_if_error(thermostat_leds_off_with_fade_eased(THERMOSTAT_ANIM_LED_BLACK_FADE_OUT_MS),
                   "boot fade-off");
      ESP_LOGI(TAG, "Boot hold done; fading off (ease-out) before handoff");
      schedule_timer(TIMER_STAGE_BOOT_COMPLETE, THERMOSTAT_ANIM_LED_BLACK_FADE_COMPLETE_DELAY_MS);
      break;
    case TIMER_STAGE_BOOT_COMPLETE:
      s_status.boot_sequence_active = false;
      s_status.timer_stage = TIMER_STAGE_NONE;
      ESP_LOGI(TAG, "Boot LED sequence finished; deferring to HVAC state");
      apply_hvac_effect();
      break;
    case TIMER_STAGE_TIMED_EFFECT_TIMEOUT:
      s_status.timed_effect_active = false;
      s_status.timer_stage = TIMER_STAGE_NONE;
      ESP_LOGI(TAG, "Timed effect timeout; restoring HVAC state");
      apply_hvac_effect();
      break;
    default:
      break;
  }
}

static void log_if_error(esp_err_t err, const char *stage)
{
  if (err != ESP_OK)
  {
    ESP_LOGW(TAG, "%s failed: %s", stage, esp_err_to_name(err));
  }
}

static void start_boot_success_sequence(void)
{
  s_status.boot_sequence_active = true;
  ESP_LOGI(TAG, "Boot sequence complete; running success fade");
  thermostat_leds_notify_boot_complete();
  log_if_error(
      thermostat_leds_solid_with_fade(thermostat_led_color(0xff, 0xff, 0xff),
                                      THERMOSTAT_ANIM_LED_WHITE_FADE_IN_MS),
      "boot fade-in");
  thermostat_splash_begin_white_fade();
  if (!s_status.timer)
  {
    ESP_LOGW(TAG, "LED status timer unavailable; skipping boot hold");
    try_play_boot_chime();
    thermostat_splash_begin_fade();
    log_if_error(thermostat_leds_off_with_fade_eased(THERMOSTAT_ANIM_LED_BLACK_FADE_OUT_MS),
                 "boot fade-off");
    s_status.boot_sequence_active = false;
    apply_hvac_effect();
    return;
  }
  schedule_timer(TIMER_STAGE_BOOT_WHITE_PEAK, THERMOSTAT_ANIM_LED_WHITE_FADE_IN_MS);
}

static void try_play_boot_chime(void)
{
#if CONFIG_THEO_AUDIO_ENABLE
  if (xTaskCreatePinnedToCoreWithCaps(boot_chime_task,
                                      "boot_chime",
                                      3072,
                                      NULL,
                                      4,
                                      NULL,
                                      tskNO_AFFINITY,
                                      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS)
  {
    ESP_LOGW(TAG, "Boot chime task create failed; playing inline");
    boot_chime_task(NULL);
  }
#else
  ESP_LOGI(TAG, "Boot chime skipped: application audio disabled");
#endif
}

static void boot_chime_task(void *arg)
{
  (void)arg;
#if CONFIG_THEO_AUDIO_ENABLE
  esp_err_t boot_audio_err = thermostat_audio_boot_try_play();
  if (boot_audio_err != ESP_OK)
  {
    ESP_LOGW(TAG, "Boot chime attempt failed: %s", esp_err_to_name(boot_audio_err));
  }
#endif
  vTaskDelete(NULL);
}

static void start_bias_lighting(void)
{
  // White at 50% brightness, 100ms fade
  log_if_error(
      thermostat_leds_solid_with_fade_brightness(thermostat_led_color(0xff, 0xff, 0xff), 100, 0.5f),
      "bias lighting");
  s_status.bias_lighting_active = true;
}

void thermostat_led_status_on_screen_wake(void)
{
  if (!s_status.leds_ready)
  {
    return;
  }

  s_status.screen_on = true;

  // If HVAC is active, restore the higher-priority wave effect (bias lighting remains suppressed).
  if (s_status.heating || s_status.cooling)
  {
    ESP_LOGD(TAG, "Screen wake: restoring HVAC wave");
    apply_hvac_effect();
    return;
  }

  // Don't start bias lighting if a higher-priority effect is active
  if (s_status.booting || s_status.boot_sequence_active || s_status.timed_effect_active)
  {
    ESP_LOGD(TAG, "Screen wake: skipping bias lighting (effect active)");
    return;
  }

  ESP_LOGI(TAG, "Screen wake: starting bias lighting");
  start_bias_lighting();
}

void thermostat_led_status_on_screen_sleep(void)
{
  if (!s_status.leds_ready)
  {
    return;
  }

  s_status.screen_on = false;
  s_status.bias_lighting_active = false;

  ESP_LOGI(TAG, "Screen sleep: fading LEDs off");
  log_if_error(thermostat_leds_off_with_fade(100), "screen sleep fade-off");
}
