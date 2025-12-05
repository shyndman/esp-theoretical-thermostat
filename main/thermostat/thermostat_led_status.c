#include "thermostat/thermostat_led_status.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "thermostat/thermostat_leds.h"

#define LED_STATUS_SPARKLE_POLL_MS (20)

static const char *TAG = "led_status";

typedef enum {
  TIMER_STAGE_NONE = 0,
  TIMER_STAGE_BOOT_WAIT_SPARKLE,
  TIMER_STAGE_BOOT_HOLD,
  TIMER_STAGE_BOOT_COMPLETE,
} timer_stage_t;

static struct {
  bool leds_ready;
  bool booting;
  bool boot_sequence_active;
  bool heating;
  bool cooling;
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

static void apply_hvac_effect(void)
{
  if (!s_status.leds_ready || s_status.booting || s_status.boot_sequence_active)
  {
    return;
  }

  if (s_status.heating)
  {
    log_if_error(thermostat_leds_pulse(thermostat_led_color(0xe1, 0x75, 0x2e), 1.0f),
                 "heating pulse");
  }
  else if (s_status.cooling)
  {
    log_if_error(thermostat_leds_pulse(thermostat_led_color(0x27, 0x76, 0xcc), 1.0f),
                 "cooling pulse");
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
    case TIMER_STAGE_BOOT_HOLD:
      log_if_error(thermostat_leds_off_with_fade_eased(2000), "boot fade-off");
      ESP_LOGI(TAG, "Boot hold done; fading off (ease-out) before handoff");
      schedule_timer(TIMER_STAGE_BOOT_COMPLETE, 2100);
      break;
    case TIMER_STAGE_BOOT_COMPLETE:
      s_status.boot_sequence_active = false;
      s_status.timer_stage = TIMER_STAGE_NONE;
      ESP_LOGI(TAG, "Boot LED sequence finished; deferring to HVAC state");
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
  log_if_error(thermostat_leds_solid_with_fade(thermostat_led_color(0xff, 0xff, 0xff), 600),
               "boot fade-in");
  if (!s_status.timer)
  {
    ESP_LOGW(TAG, "LED status timer unavailable; skipping boot hold");
    log_if_error(thermostat_leds_off_with_fade_eased(2000), "boot fade-off");
    s_status.boot_sequence_active = false;
    apply_hvac_effect();
    return;
  }
  schedule_timer(TIMER_STAGE_BOOT_HOLD, 1200);
}
