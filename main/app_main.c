#include <stdarg.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "bsp/display.h"
#include "bsp/touch.h"
#include "esp_lv_adapter.h"
#include "esp_lv_adapter_display.h"
#include "esp_lv_adapter_input.h"
#include "lvgl.h"
#include "thermostat_ui.h"
#include "thermostat/backlight_manager.h"
#include "thermostat/audio_boot.h"
#include "thermostat/thermostat_led_status.h"
#include "thermostat/ui_splash.h"
#include "connectivity/esp_hosted_link.h"
#include "connectivity/wifi_remote_manager.h"
#include "connectivity/time_sync.h"
#include "connectivity/mqtt_manager.h"
#include "connectivity/mqtt_dataplane.h"

static const char *TAG = "theo";

static void splash_status_printf(thermostat_splash_t *splash, const char *fmt, ...)
{
  if (!splash || !fmt)
  {
    return;
  }

  char buffer[96];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buffer, sizeof(buffer), fmt, args);
  va_end(args);
  thermostat_splash_set_status(splash, buffer);
}

static void boot_fail(thermostat_splash_t *splash, const char *stage, esp_err_t err)
{
  if (splash)
  {
    thermostat_splash_show_error(splash, stage, err);
  }

  esp_err_t audio_err = thermostat_audio_boot_play_failure();
  if (audio_err != ESP_OK)
  {
    ESP_LOGW(TAG, "Failure tone suppressed: %s", esp_err_to_name(audio_err));
  }

  while (true)
  {
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

void app_main(void)
{
  bsp_lcd_handles_t handles = {0};
  esp_err_t led_err = thermostat_led_status_init();
  if (led_err != ESP_OK)
  {
    ESP_LOGW(TAG, "LED status init skipped: %s", esp_err_to_name(led_err));
  }
  thermostat_led_status_booting();

  ESP_LOGI(TAG, "Pre BSP display with handles");
  if (bsp_display_new_with_handles(NULL, &handles) != ESP_OK)
  {
    ESP_LOGE(TAG, "Failed to create BSP display handles");
    return;
  }

  // Step 1: Initialize the adapter
  esp_lv_adapter_config_t cfg = ESP_LV_ADAPTER_DEFAULT_CONFIG();
  ESP_ERROR_CHECK(esp_lv_adapter_init(&cfg));

  // Step 2: Register a display (choose macro by interface)
  esp_lv_adapter_display_config_t disp_cfg = ESP_LV_ADAPTER_DISPLAY_MIPI_DEFAULT_CONFIG(
      handles.panel,
      handles.io,
      BSP_LCD_H_RES,
      BSP_LCD_V_RES,
      ESP_LV_ADAPTER_ROTATE_0);
  disp_cfg.profile.use_psram = true;
  disp_cfg.profile.enable_ppa_accel = false;

  lv_display_t *disp = esp_lv_adapter_register_display(&disp_cfg);
  if (disp == NULL)
  {
    ESP_LOGE(TAG, "esp_lv_adapter_register_display failed");
    return;
  }

  // Step 3: (Optional) Register input device(s)
  esp_lcd_touch_handle_t touch = NULL;
  if (bsp_touch_new(NULL, &touch) != ESP_OK)
  {
    ESP_LOGW(TAG, "Touch controller not initialized");
    return;
  }

  esp_lv_adapter_touch_config_t touch_cfg = ESP_LV_ADAPTER_TOUCH_DEFAULT_CONFIG(disp, touch);
  if (esp_lv_adapter_register_touch(&touch_cfg) == NULL)
  {
    ESP_LOGW(TAG, "Failed to register touch device");
  }

  // Step 4: Start the adapter task
  ESP_ERROR_CHECK(esp_lv_adapter_start());

  backlight_manager_config_t backlight_cfg = {
      .disp = disp,
  };
  ESP_ERROR_CHECK(backlight_manager_init(&backlight_cfg));
  ESP_LOGI(TAG, "Backlight manager initialized");

  thermostat_splash_t *splash = thermostat_splash_create(disp);
  if (!splash)
  {
    ESP_LOGE(TAG, "Failed to create splash screen; aborting boot");
    return;
  }

  splash_status_printf(splash, "Preparing speaker...");
  esp_err_t err = thermostat_audio_boot_prepare();
  if (err != ESP_OK)
  {
    ESP_LOGE(TAG, "Speaker prepare failed: %s", esp_err_to_name(err));
    boot_fail(splash, "prepare speaker", err);
  }

  splash_status_printf(splash, "Starting esp-hosted link...");
  err = esp_hosted_link_start();
  if (err != ESP_OK)
  {
    ESP_LOGE(TAG, "ESP-Hosted link failed to initialize");
    boot_fail(splash, "start esp-hosted link", err);
  }

  splash_status_printf(splash, "Starting Wi-Fi stack...");
  err = wifi_remote_manager_start();
  if (err != ESP_OK)
  {
    ESP_LOGE(TAG, "Wi-Fi bring-up failed");
    boot_fail(splash, "start Wi-Fi", err);
  }

  splash_status_printf(splash, "Syncing time...");
  err = time_sync_start();
  if (err != ESP_OK)
  {
    ESP_LOGE(TAG, "time_sync_start failed: %s", esp_err_to_name(err));
    boot_fail(splash, "start time sync", err);
  }
  if (!time_sync_wait_for_sync(pdMS_TO_TICKS(30000)))
  {
    ESP_LOGW(TAG, "SNTP sync timeout; continuing without wall clock");
  }

  splash_status_printf(splash, "Starting MQTT client...");
  err = mqtt_manager_start();
  if (err != ESP_OK)
  {
    ESP_LOGE(TAG, "MQTT startup failed; halting boot");
    boot_fail(splash, "start MQTT client", err);
  }

  splash_status_printf(splash, "Starting MQTT dataplane...");
  err = mqtt_dataplane_start();
  if (err != ESP_OK)
  {
    ESP_LOGE(TAG, "MQTT dataplane startup failed; halting boot");
    boot_fail(splash, "start MQTT dataplane", err);
  }

  splash_status_printf(splash, "Loading thermostat UI...");
  if (esp_lv_adapter_lock(-1) == ESP_OK)
  {
    thermostat_ui_attach();
    esp_lv_adapter_unlock();
  }

  thermostat_splash_destroy(splash);
  thermostat_led_status_boot_complete();

  backlight_manager_on_ui_ready();

  esp_err_t boot_audio_err = thermostat_audio_boot_try_play();
  if (boot_audio_err != ESP_OK)
  {
    ESP_LOGW(TAG, "Boot chime attempt failed: %s", esp_err_to_name(boot_audio_err));
  }

  while (true)
  {
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
