#include <stdarg.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_ota_ops.h"
#include "esp_timer.h"
#include "bsp/display.h"
#include "bsp/esp32_p4_nano.h"
#include "bsp/touch.h"
#include "esp_lv_adapter.h"
#include "esp_lv_adapter_display.h"
#include "esp_lv_adapter_input.h"
#include "lvgl.h"
#include "thermostat_ui.h"
#include "thermostat/backlight_manager.h"
#include "thermostat/audio_boot.h"
#include "thermostat/thermostat_led_status.h"
#include "thermostat/ui_animation_timing.h"
#include "thermostat/ui_ota_modal.h"
#include "thermostat/ui_splash.h"
#include "connectivity/esp_hosted_link.h"
#include "connectivity/ota_server.h"
#include "connectivity/wifi_remote_manager.h"
#include "connectivity/time_sync.h"
#include "connectivity/mqtt_manager.h"
#include "connectivity/mqtt_dataplane.h"
#include "connectivity/device_info.h"
#include "connectivity/device_telemetry.h"
#include "connectivity/device_ip_publisher.h"
#include "sensors/env_sensors.h"
#include "sensors/radar_presence.h"
#if CONFIG_THEO_CAMERA_ENABLE
#include "streaming/mjpeg_stream.h"
#endif

static const char *TAG = "theo";
static void splash_post_fade_boot_continuation(void *ctx);
static void dataplane_status_cb(const char *status, void *ctx);
static void splash_status_printf(thermostat_splash_t *splash, const char *fmt, ...);
static void splash_status_color_printf(thermostat_splash_t *splash,
                                       lv_color_t color,
                                       const char *fmt,
                                       ...);
static esp_err_t radar_start_with_timeout(thermostat_splash_t *splash, uint32_t timeout_ms);
static void ota_start_cb(size_t total_bytes, void *ctx);
static void ota_progress_cb(size_t written_bytes, size_t total_bytes, void *ctx);
static void ota_error_cb(const char *message, void *ctx);
static void ota_validate_running_partition(void);

#define RADAR_START_TIMEOUT_MS (10000)
#define RADAR_TIMEOUT_STATUS_COLOR_HEX (0xff6666)
#define SPLASH_FINAL_STATUS_COLOR_HEX (0xffffff)

static int64_t boot_stage_start(thermostat_splash_t *splash, const char *label)
{
  ESP_LOGI(TAG, "[boot] %s", label);
  splash_status_printf(splash, "%s", label);
  return esp_timer_get_time();
}

static void boot_stage_done(const char *label, int64_t start_us)
{
  int64_t elapsed_ms = (esp_timer_get_time() - start_us) / 1000;
  ESP_LOGI(TAG, "[boot] %s done (%lld ms)", label, (long long)elapsed_ms);
}

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

static void splash_status_color_printf(thermostat_splash_t *splash,
                                       lv_color_t color,
                                       const char *fmt,
                                       ...)
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
  thermostat_splash_set_status_color(splash, buffer, color);
}

typedef struct
{
  TaskHandle_t task;
  SemaphoreHandle_t done;
  esp_err_t result;
} radar_start_ctx_t;

static void radar_start_task(void *arg)
{
  radar_start_ctx_t *ctx = (radar_start_ctx_t *)arg;
  if (!ctx)
  {
    vTaskDelete(NULL);
    return;
  }
  ctx->result = radar_presence_start();
  if (ctx->done)
  {
    xSemaphoreGive(ctx->done);
  }
  vTaskDelete(NULL);
}

static esp_err_t radar_start_with_timeout(thermostat_splash_t *splash, uint32_t timeout_ms)
{
  radar_start_ctx_t *ctx = calloc(1, sizeof(*ctx));
  if (!ctx)
  {
    return ESP_ERR_NO_MEM;
  }

  *ctx = (radar_start_ctx_t){
      .task = NULL,
      .done = xSemaphoreCreateBinary(),
      .result = ESP_FAIL,
  };
  if (ctx->done == NULL)
  {
    free(ctx);
    return ESP_ERR_NO_MEM;
  }

  BaseType_t task_ok = xTaskCreatePinnedToCoreWithCaps(radar_start_task,
                                   "radar_start",
                                   6144,
                                   ctx,
                                   4,
                                   &ctx->task,
                                   tskNO_AFFINITY,
                                   MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (task_ok != pdPASS)
  {
    vSemaphoreDelete(ctx->done);
    free(ctx);
    return ESP_ERR_NO_MEM;
  }

  if (xSemaphoreTake(ctx->done, pdMS_TO_TICKS(timeout_ms)) != pdTRUE)
  {
    splash_status_color_printf(splash,
                               lv_color_hex(RADAR_TIMEOUT_STATUS_COLOR_HEX),
                               "Radar init timed out; continuing");
    ESP_LOGW(TAG, "[boot] radar init timed out after %u ms; continuing", timeout_ms);
    vSemaphoreDelete(ctx->done);
    free(ctx);
    return ESP_ERR_TIMEOUT;
  }

  esp_err_t result = ctx->result;
  vSemaphoreDelete(ctx->done);
  free(ctx);
  if (result == ESP_OK)
  {
    ESP_LOGI(TAG, "[boot] radar init completed within %u ms", timeout_ms);
  }
  return result;
}

static void boot_fail(thermostat_splash_t *splash, const char *stage, esp_err_t err)
{
  ESP_LOGE(TAG, "[boot] stage failed: %s (%s)", stage, esp_err_to_name(err));
  if (splash)
  {
    thermostat_splash_show_error(splash, stage, err);
  }

  esp_err_t audio_err = thermostat_audio_boot_play_failure();
  if (audio_err != ESP_OK)
  {
    ESP_LOGW(TAG, "Failure tone suppressed: %s", esp_err_to_name(audio_err));
  }

  vTaskDelay(pdMS_TO_TICKS(5000));
  esp_restart();
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

  // Initialize I2C and turn off backlight BEFORE panel init to prevent white flash
  bsp_i2c_init();
  bsp_display_backlight_off();

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

  int64_t stage_start_us = 0;
  esp_err_t err = ESP_OK;

#if CONFIG_THEO_AUDIO_ENABLE
  stage_start_us = boot_stage_start(splash, "Preparing I2S audio…");
  err = thermostat_audio_boot_prepare();
  if (err != ESP_OK)
  {
    ESP_LOGE(TAG, "Speaker prepare failed: %s", esp_err_to_name(err));
    boot_fail(splash, "prepare speaker", err);
  }
  boot_stage_done("Preparing speaker…", stage_start_us);
#else
  ESP_LOGI(TAG, "Application audio disabled; skipping speaker prep");
#endif

  stage_start_us = boot_stage_start(splash, "Establishing co-processor link…");
  err = esp_hosted_link_start();
  if (err != ESP_OK)
  {
    ESP_LOGE(TAG, "ESP-Hosted link failed to initialize");
    boot_fail(splash, "start esp-hosted link", err);
  }
  boot_stage_done("Establishing co-processor link…", stage_start_us);

  stage_start_us = boot_stage_start(splash, "Enabling Wi-Fi…");
  err = wifi_remote_manager_start();
  if (err != ESP_OK)
  {
    ESP_LOGE(TAG, "Wi-Fi bring-up failed");
    boot_fail(splash, "start Wi-Fi", err);
  }
  boot_stage_done("Enabling Wi-Fi…", stage_start_us);

  ota_server_callbacks_t ota_callbacks = {
      .on_start = ota_start_cb,
      .on_progress = ota_progress_cb,
      .on_error = ota_error_cb,
      .ctx = NULL,
  };
  err = ota_server_start(&ota_callbacks);
  if (err != ESP_OK)
  {
    ESP_LOGE(TAG, "OTA server start failed: %s", esp_err_to_name(err));
  }

#if CONFIG_THEO_CAMERA_ENABLE
  stage_start_us = boot_stage_start(splash, "Starting MJPEG stream server…");
  err = mjpeg_stream_start();
  if (err == ESP_ERR_NOT_FOUND)
  {
    ESP_LOGW(TAG, "Camera not detected; streaming disabled");
  }
  else if (err != ESP_OK)
  {
    ESP_LOGW(TAG, "Camera stream failed: %s", esp_err_to_name(err));
  }
  boot_stage_done("Starting MJPEG stream server…", stage_start_us);
#endif

  stage_start_us = boot_stage_start(splash, "Syncing time…");
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
  boot_stage_done("Syncing time…", stage_start_us);

  stage_start_us = boot_stage_start(splash, "Connecting to broker…");
  err = mqtt_manager_start(dataplane_status_cb, splash);
  if (err != ESP_OK)
  {
    ESP_LOGE(TAG, "MQTT startup failed; halting boot");
    boot_fail(splash, "start MQTT client", err);
  }
  boot_stage_done("Connecting to broker…", stage_start_us);

  stage_start_us = boot_stage_start(splash, "Initializing data channel…");
  err = mqtt_dataplane_start(dataplane_status_cb, splash);
  if (err != ESP_OK)
  {
    ESP_LOGE(TAG, "MQTT dataplane startup failed; halting boot");
    boot_fail(splash, "start MQTT dataplane", err);
  }
  boot_stage_done("Initializing data channel…", stage_start_us);

  stage_start_us = boot_stage_start(splash, "Starting environmental sensors…");
  err = env_sensors_start();
  if (err != ESP_OK)
  {
    ESP_LOGE(TAG, "Environmental sensors startup failed; halting boot");
    boot_fail(splash, "start environmental sensors", err);
  }
  boot_stage_done("Starting environmental sensors…", stage_start_us);

  err = device_info_start();
  if (err != ESP_OK)
  {
    ESP_LOGW(TAG, "Device info diagnostics startup failed: %s", esp_err_to_name(err));
  }

  err = device_telemetry_start();
  if (err != ESP_OK)
  {
    ESP_LOGW(TAG, "Device telemetry diagnostics startup failed: %s", esp_err_to_name(err));
  }

  err = device_ip_publisher_start();
  if (err != ESP_OK)
  {
    ESP_LOGW(TAG, "Device IP publisher startup failed: %s", esp_err_to_name(err));
  }

  stage_start_us = boot_stage_start(splash, "Starting radar presence sensor…");
#ifdef CONFIG_THEO_RADAR_ENABLE
  err = radar_start_with_timeout(splash, RADAR_START_TIMEOUT_MS);
  if (err == ESP_ERR_TIMEOUT)
  {
    // Continue boot; radar may still come online later.
  }
  else if (err != ESP_OK)
  {
    ESP_LOGW(TAG, "Radar presence startup failed; continuing without presence detection");
  }
  boot_stage_done("Starting radar presence sensor…", stage_start_us);
#else
  ESP_LOGI(TAG, "[boot] Radar presence disabled via CONFIG_THEO_RADAR_ENABLE; skipping init");
  splash_status_printf(splash, "Radar presence disabled; skipping init");
  boot_stage_done("Radar presence disabled", stage_start_us);
#endif

  stage_start_us = boot_stage_start(splash, "Waiting for thermostat state…");
  err = mqtt_dataplane_await_initial_state(dataplane_status_cb, splash, 30000);
  if (err != ESP_OK)
  {
    ESP_LOGE(TAG, "Timed out waiting for thermostat state");
    boot_fail(splash, "receive thermostat state", err);
  }
  boot_stage_done("Waiting for thermostat state…", stage_start_us);

  stage_start_us = boot_stage_start(splash, "Loading thermostat UI…");

  bool splash_animating = thermostat_splash_is_animating(splash);
  esp_err_t final_status_err =
      thermostat_splash_finalize_status(splash,
                                        "Starting…",
                                        lv_color_hex(SPLASH_FINAL_STATUS_COLOR_HEX));
  if (final_status_err == ESP_OK)
  {
    uint32_t final_hold_ms = THERMOSTAT_ANIM_SPLASH_LINE_ENTER_MS +
                             THERMOSTAT_ANIM_SPLASH_FINAL_HOLD_MS;
    if (splash_animating)
    {
      final_hold_ms += THERMOSTAT_ANIM_SPLASH_LINE_ENTER_MS;
    }
    vTaskDelay(pdMS_TO_TICKS(final_hold_ms));
  }
  else
  {
    ESP_LOGW(TAG, "Final splash status skipped: %s", esp_err_to_name(final_status_err));
  }

  thermostat_splash_destroy(splash, splash_post_fade_boot_continuation, NULL);
  thermostat_led_status_boot_complete();
  ESP_LOGI(TAG, "[boot] UI created; LED ceremony started; waiting for splash fade");
  boot_stage_done("Loading thermostat UI...", stage_start_us);

  while (true)
  {
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

static void dataplane_status_cb(const char *status, void *ctx)
{
  thermostat_splash_t *splash = (thermostat_splash_t *)ctx;
  if (splash && status)
  {
    thermostat_splash_set_status(splash, status);
  }
}

static void splash_post_fade_boot_continuation(void *ctx)
{
  (void)ctx;

  thermostat_ui_attach();
  thermostat_ui_refresh_all();

  backlight_manager_on_ui_ready();
  ota_validate_running_partition();
}

static void ota_start_cb(size_t total_bytes, void *ctx)
{
  (void)ctx;
  ESP_LOGI(TAG, "OTA upload starting (%zu bytes)", total_bytes);
  esp_err_t err = backlight_manager_set_hold(true);
  if (err != ESP_OK)
  {
    ESP_LOGW(TAG, "OTA backlight hold failed: %s", esp_err_to_name(err));
  }
#if CONFIG_THEO_CAMERA_ENABLE
  mjpeg_stream_stop();
#endif
  err = thermostat_ota_modal_show(total_bytes);
  if (err != ESP_OK)
  {
    ESP_LOGW(TAG, "OTA modal show failed: %s", esp_err_to_name(err));
    backlight_manager_set_hold(false);
  }
}

static void ota_progress_cb(size_t written_bytes, size_t total_bytes, void *ctx)
{
  (void)ctx;
  esp_err_t err = thermostat_ota_modal_update(written_bytes, total_bytes);
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
  {
    ESP_LOGW(TAG, "OTA modal update failed: %s", esp_err_to_name(err));
  }
}

static void ota_error_cb(const char *message, void *ctx)
{
  (void)ctx;
  ESP_LOGE(TAG, "OTA error: %s", message ? message : "unknown");
  esp_err_t err = thermostat_ota_modal_show_error(message);
  if (err != ESP_OK)
  {
    ESP_LOGW(TAG, "OTA modal error display failed: %s", esp_err_to_name(err));
    backlight_manager_set_hold(false);
  }
}

static void ota_validate_running_partition(void)
{
  const esp_partition_t *running = esp_ota_get_running_partition();
  if (running == NULL)
  {
    ESP_LOGW(TAG, "[ota] running partition not found");
    return;
  }

  esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
  esp_err_t err = esp_ota_get_state_partition(running, &state);
  if (err != ESP_OK)
  {
    ESP_LOGW(TAG, "[ota] failed to read partition state: %s", esp_err_to_name(err));
    return;
  }

  if (state != ESP_OTA_IMG_PENDING_VERIFY)
  {
    return;
  }

  err = esp_ota_mark_app_valid_cancel_rollback();
  if (err != ESP_OK)
  {
    ESP_LOGW(TAG, "[ota] rollback validation failed: %s", esp_err_to_name(err));
    return;
  }

  ESP_LOGI(TAG, "[ota] running image marked valid");
}
