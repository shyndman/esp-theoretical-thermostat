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
#include "connectivity/esp_hosted_link.h"
#include "connectivity/wifi_remote_manager.h"
#include "connectivity/time_sync.h"
#include "connectivity/mqtt_manager.h"

static const char *TAG = "theo";

void app_main(void)
{
    ESP_LOGI(TAG, "Starting esp-hosted SDIO link");
    if (esp_hosted_link_start() != ESP_OK) {
        ESP_LOGE(TAG, "ESP-Hosted link failed to initialize");
        return;
    }

    ESP_LOGI(TAG, "Starting Wi-Fi stack via esp_wifi_remote");
    if (wifi_remote_manager_start() != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi bring-up failed");
        return;
    }

    ESP_ERROR_CHECK(time_sync_start());
    if (!time_sync_wait_for_sync(pdMS_TO_TICKS(10000))) {
        ESP_LOGW(TAG, "SNTP sync timeout; continuing without wall clock");
    }

    ESP_LOGI(TAG, "Starting MQTT client over WebSocket");
    if (mqtt_manager_start() != ESP_OK) {
        ESP_LOGE(TAG, "MQTT startup failed; halting boot");
        return;
    }

    bsp_lcd_handles_t handles = {0};
    ESP_LOGI(TAG, "Pre BSP display with handles");
    if (bsp_display_new_with_handles(NULL, &handles) != ESP_OK) {
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
    if (disp == NULL) {
        ESP_LOGE(TAG, "esp_lv_adapter_register_display failed");
        return;
    }

    // Step 3: (Optional) Register input device(s)
    esp_lcd_touch_handle_t touch = NULL;
    if (bsp_touch_new(NULL, &touch) != ESP_OK) {
        ESP_LOGW(TAG, "Touch controller not initialized");
        return;
    }

    esp_lv_adapter_touch_config_t touch_cfg = ESP_LV_ADAPTER_TOUCH_DEFAULT_CONFIG(disp, touch);
    if (esp_lv_adapter_register_touch(&touch_cfg) == NULL) {
        ESP_LOGW(TAG, "Failed to register touch device");
    }

    // Step 4: Start the adapter task
    ESP_ERROR_CHECK(esp_lv_adapter_start());

    backlight_manager_config_t backlight_cfg = {
        .disp = disp,
    };
    ESP_ERROR_CHECK(backlight_manager_init(&backlight_cfg));
    ESP_LOGI(TAG, "Backlight manager initialized");

    // Step 5: Draw with LVGL (guarded by adapter lock for thread safety)
    if (esp_lv_adapter_lock(-1) == ESP_OK) {
        thermostat_ui_attach();
        esp_lv_adapter_unlock();
    }

    backlight_manager_on_ui_ready();

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
