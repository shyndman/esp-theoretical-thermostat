#include "thermostat/ir_led.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "sdkconfig.h"

typedef struct {
  bool initialized;
  bool available;
  bool is_on;
  esp_err_t init_err;
} ir_led_state_t;

static ir_led_state_t s_state = {
  .initialized = false,
  .available = false,
  .is_on = false,
  .init_err = ESP_ERR_INVALID_STATE,
};

static const char *TAG = "ir_led";

esp_err_t thermostat_ir_led_init(void)
{
  if (s_state.initialized)
  {
    return s_state.init_err;
  }

  gpio_config_t config = {
    .pin_bit_mask = 1ULL << CONFIG_THEO_IR_LED_GPIO,
    .mode = GPIO_MODE_OUTPUT,
    .pull_up_en = GPIO_PULLUP_DISABLE,
    .pull_down_en = GPIO_PULLDOWN_DISABLE,
    .intr_type = GPIO_INTR_DISABLE,
  };

  esp_err_t err = gpio_config(&config);
  if (err != ESP_OK)
  {
    ESP_LOGW(TAG, "IR LED GPIO config failed: %s", esp_err_to_name(err));
    s_state.initialized = true;
    s_state.available = false;
    s_state.init_err = err;
    return err;
  }

  err = gpio_set_level(CONFIG_THEO_IR_LED_GPIO, 0);
  if (err != ESP_OK)
  {
    ESP_LOGW(TAG, "IR LED GPIO default low failed: %s", esp_err_to_name(err));
    s_state.initialized = true;
    s_state.available = false;
    s_state.init_err = err;
    return err;
  }

  s_state.initialized = true;
  s_state.available = true;
  s_state.is_on = false;
  s_state.init_err = ESP_OK;
  ESP_LOGI(TAG, "IR LED ready on GPIO %d", CONFIG_THEO_IR_LED_GPIO);
  return ESP_OK;
}

void thermostat_ir_led_set(bool on)
{
  if (!s_state.initialized || !s_state.available)
  {
    return;
  }

  if (s_state.is_on == on)
  {
    return;
  }

  esp_err_t err = gpio_set_level(CONFIG_THEO_IR_LED_GPIO, on ? 1 : 0);
  if (err != ESP_OK)
  {
    ESP_LOGW(TAG, "IR LED set %s failed: %s", on ? "on" : "off", esp_err_to_name(err));
    return;
  }

  s_state.is_on = on;
  ESP_LOGI(TAG, "IR LED %s", on ? "on" : "off");
}
