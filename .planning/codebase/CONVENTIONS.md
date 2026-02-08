# Coding Conventions

**Analysis Date:** 2026-02-08

## Naming Patterns

**Files:**
- C source files use `snake_case.c`: `mqtt_manager.c`, `backlight_manager.c`
- Header files use `snake_case.h`: `mqtt_manager.h`, `ui_state.h`
- Private headers are co-located with implementation, public headers in `include/` dirs
- Test files use `test_*.c` pattern: `test_http_server.c`

**Functions:**
- Module-prefixed snake_case: `mqtt_manager_start()`, `backlight_manager_init()`
- Private static functions use module prefix without exposing in headers
- Event handlers: `{module}_event_handler()`, `wifi_event_handler()`
- Callback functions: Named with `_cb` suffix: `ota_start_cb()`, `dataplane_status_cb()`

**Variables:**
- Static module variables prefixed with `s_`: `s_client`, `s_wifi_event_group`
- Global variables prefixed with `g_`: `g_view_model`, `g_fonts`, `g_root_screen`
- Constants use UPPER_SNAKE_CASE with module prefix: `MQTT_KEEPALIVE_SECONDS`, `MQTT_DP_QUEUE_DEPTH`
- Ext RAM attributed variables use `EXT_RAM_BSS_ATTR`: `EXT_RAM_BSS_ATTR char s_broker_uri[160]`

**Types:**
- Structs: `typedef struct { ... } thermostat_view_model_t;`
- Enums: `typedef enum { ... } thermostat_target_t;`
- Type names use `_t` suffix: `mqtt_manager_status_cb_t`, `dp_msg_type_t`

**Macros:**
- Use UPPER_SNAKE_CASE with module prefix: `THERMOSTAT_MIN_TEMP_C`, `MQTT_TOPIC_MAX_LEN`
- Config macros use `CONFIG_THEO_*` prefix (from Kconfig): `CONFIG_THEO_MQTT_HOST`, `CONFIG_THEO_BACKLIGHT_TIMEOUT_SECONDS`

## Code Style

**Formatting:**
- 2-space indentation, no tabs
- Line wrap at ~100 columns
- Opening braces on same line for functions and control structures

**Function signatures:**
```c
esp_err_t mqtt_manager_start(mqtt_manager_status_cb_t status_cb, void *ctx)
{
  if (s_client_started) {
    return ESP_OK;
  }
  // ...
}
```

**Error handling:**
- Prefer early returns with `ESP_RETURN_ON_*` macros from `esp_check.h`
```c
esp_err_t wifi_remote_manager_start(void)
{
  if (s_ready) {
    return ESP_OK;
  }

  ESP_RETURN_ON_FALSE(strlen(CONFIG_THEO_WIFI_STA_SSID) > 0, ESP_ERR_INVALID_STATE, TAG,
                      "CONFIG_THEO_WIFI_STA_SSID must be set");
  ESP_RETURN_ON_ERROR(init_nvs(), TAG, "nvs init failed");
  // ...
}
```

## Import Organization

**Order in source files:**
1. Corresponding header: `"thermostat/backlight_manager.h"`
2. Standard library: `<string.h>`, `<stdio.h>`, `<stdlib.h>`
3. ESP-IDF system: `"esp_log.h"`, `"esp_err.h"`, `"esp_check.h"`, `"esp_timer.h"`
4. FreeRTOS: `"freertos/FreeRTOS.h"`, `"freertos/task.h"`, `"freertos/queue.h"`
5. ESP-IDF components: `"mqtt_client.h"`, `"nvs_flash.h"`, `"esp_wifi.h"`
6. LVGL: `"lvgl.h"`, `"esp_lv_adapter.h"`
7. Project headers: `"thermostat/ui_state.h"`, `"connectivity/mqtt_manager.h"`
8. Generated assets: `LV_IMG_DECLARE()` macros

**Path conventions:**
- Project headers use relative paths: `"thermostat/ui_state.h"`, `"connectivity/mqtt_manager.h"`
- No path aliases configured; direct relative paths from `main/` root

## Error Handling

**Patterns:**
- Use `esp_err_t` return type for most functions
- Use `ESP_RETURN_ON_ERROR()` for propagating errors with logging
- Use `ESP_RETURN_ON_FALSE()` for argument/state validation
- Use `ESP_ERROR_CHECK()` only for fatal initialization errors

**Example from `app_main.c`:**
```c
esp_err_t err = wifi_remote_manager_start();
if (err != ESP_OK)
{
  ESP_LOGE(TAG, "Wi-Fi bring-up failed");
  boot_fail(splash, "start Wi-Fi", err);
}
```

**Logging pattern:**
- Include error name in logs: `ESP_LOGE(TAG, "Failed: %s", esp_err_to_name(err));`
- Use descriptive context in brackets: `[boot]`, `[antiburn]`, `[presence]`

## Logging

**Framework:** ESP-IDF logging (`esp_log.h`)

**Patterns:**
```c
static const char *TAG = "mqtt";  // Module tag at file scope

ESP_LOGI(TAG, "MQTT client starting (id=%s)", s_client_id);  // Info
ESP_LOGW(TAG, "Subscribe failed topic=%s", topic);           // Warning  
ESP_LOGE(TAG, "Failed to create queue");                      // Error
ESP_LOGD(TAG, "[idle] interaction reason=%s", reason_name);   // Debug
```

**TAG conventions:**
- Short lowercase module identifier: `"mqtt"`, `"theo"`, `"backlight"`
- Single word where possible, max 8 characters

## Comments

**When to Comment:**
- Complex algorithmic sections explain the "why"
- Configuration boundaries: `// If waking from idle, the touch was consumed; don't also sleep`
- Hardware-related workarounds: `// Initialize I2C and turn off backlight BEFORE panel init`

**Header documentation:**
- Use block comments for complex struct/enum definitions
- Include usage warnings: `/* >>> READ THIS BEFORE TOUCHING ANY COLORS <<< */`

## Function Design

**Size:** Functions are typically 20-100 lines; complex logic split into static helpers

**Parameters:**
- Context pointers as last parameter: `void *ctx`, `void *user_ctx`
- Output buffers include size: `char *buffer, size_t buffer_len`

**Return Values:**
- `esp_err_t` for success/failure indication
- `bool` for state queries: `wifi_remote_manager_is_ready()`
- `void` for event handlers and callbacks

## Module Design

**Exports:**
- Public API in header files
- Internal functions marked `static`
- Module state in static file-scope variables

**Example header pattern (`mqtt_manager.h`):**
```c
#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "mqtt_client.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*mqtt_manager_status_cb_t)(const char *status, void *ctx);

esp_err_t mqtt_manager_start(mqtt_manager_status_cb_t status_cb, void *ctx);
bool mqtt_manager_is_ready(void);
const char *mqtt_manager_uri(void);
esp_mqtt_client_handle_t mqtt_manager_get_client(void);

#ifdef __cplusplus
}
#endif
```

**State management:**
- Static module state structs: `static backlight_state_t s_state;`
- Global view model for UI state: `thermostat_view_model_t g_view_model`

## Memory Management

**Allocation patterns:**
- Use `calloc()` for zero-initialized allocations: `ctx = calloc(1, sizeof(*ctx));`
- Prefer SPIRAM for large buffers: `MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT`
- Check allocation results before use

**FreeRTOS patterns:**
- Tasks created with `xTaskCreatePinnedToCoreWithCaps()` for PSRAM allocation
- Queues: `xQueueCreate(depth, item_size)`
- Semaphores: `xSemaphoreCreateBinary()`, `vSemaphoreDelete()`

## LVGL Conventions

**Locking:** Always acquire LVGL lock before UI operations:
```c
if (esp_lv_adapter_lock(-1) == ESP_OK) {
    g_view_model.weather_ready = true;
    if (g_ui_initialized) {
        thermostat_update_weather_group();
    }
    esp_lv_adapter_unlock();
} else {
    ESP_LOGW(TAG, "LVGL lock timeout updating weather temp");
}
```

**Object naming:** LVGL objects use `lv_obj_t *` with descriptive names: `g_action_bar`, `g_mode_icon`

---

*Convention analysis: 2026-02-08*
