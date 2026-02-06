# Change: Refactor anti-burn static overlay

## Why
The current anti-burn (“pixel training”) implementation allocates a full-screen RGB565 buffer to render noise, which consumes a large amount of RAM/PSRAM for a feature that runs unattended.

Additionally, anti-burn currently reuses the normal day/night brightness policy via `apply_current_brightness("antiburn-start")`, rather than explicitly forcing full brightness.

## What Changes
- Replace the canvas-backed snow overlay with a procedural, draw-time static effect that does not allocate a screen-sized backing buffer.
- Keep the existing anti-burn schedule and duration (`CONFIG_THEO_ANTIBURN_DURATION_SECONDS`).
- During anti-burn, drive the display at full brightness (100%) regardless of day/night mode.
- During anti-burn, do not accept touch-driven interactions (touches are ignored/consumed and do not trigger UI actions).
- Render static as per-pixel noise with only primary/white colors (pure red/green/blue/white).

## Impact
- Affected specs: `openspec/specs/thermostat-ui-interactions/spec.md`
- Affected code: `main/thermostat/backlight_manager.c`, `main/thermostat/backlight_manager.h`
- Risk: performance regression if per-pixel static rendering exceeds the effective frame budget; mitigate by relying on best-effort redraw cadence (no hard FPS requirements) and keeping the implementation allocation-free.

## 3rd-Party Dependencies (Verified)

This change does not add new dependencies or modify dependency versions. It relies on the versions already resolved in this repo:

- **ESP-IDF**: `idf` `5.5.2` (`dependencies.lock`)
- **LVGL**: `lvgl/lvgl` `9.4.0` (`managed_components/lvgl__lvgl/idf_component.yml`, `dependencies.lock`, `managed_components/lvgl__lvgl/lv_version.h`)
- **ESP LVGL Adapter**: `espressif/esp_lvgl_adapter` `0.1.4` (`managed_components/espressif__esp_lvgl_adapter/idf_component.yml`, `dependencies.lock`)

### LVGL API usage

Verified against LVGL 9.4 headers in-tree and the LVGL 9.4 official docs:

- Draw-time overlay uses `LV_EVENT_DRAW_MAIN` (LVGL drawing event) and `lv_event_get_layer()` to obtain the draw layer.
  - Signature: `lv_layer_t * lv_event_get_layer(lv_event_t * e);` (`managed_components/lvgl__lvgl/src/core/lv_obj_event.h`)
  - Docs: https://docs.lvgl.io/9.4/details/common-widget-features/events.html
  - API: https://docs.lvgl.io/9.4/API/core/lv_obj_event_h.html

- Frame invalidation may use partial invalidation (area-based redraw) instead of full-screen invalidation.
  - Signature: `void lv_obj_invalidate_area(const lv_obj_t * obj, const lv_area_t * area);` (`managed_components/lvgl__lvgl/src/core/lv_obj_pos.h`)
  - API: https://docs.lvgl.io/9.4/API/core/lv_obj_pos_h.html

- Event registration is done via the public object event API.
  - Signature: `lv_event_dsc_t * lv_obj_add_event_cb(lv_obj_t * obj, lv_event_cb_t event_cb, lv_event_code_t filter, void * user_data);` (`managed_components/lvgl__lvgl/src/core/lv_obj_event.h`)
  - API: https://docs.lvgl.io/9.4/API/core/lv_obj_event_h.html

### esp_lvgl_adapter thread-safety expectations

Verified against `esp_lvgl_adapter` v0.1.4 public header and its official README:

- Lock/unlock MUST guard LVGL calls from user code.
  - Signatures: `esp_err_t esp_lv_adapter_lock(int32_t timeout_ms);` and `void esp_lv_adapter_unlock(void);` (`managed_components/espressif__esp_lvgl_adapter/include/esp_lv_adapter.h`)
  - Docs (README): https://github.com/espressif/esp-iot-solution/tree/master/components/display/tools/esp_lvgl_adapter

### ESP-IDF memory allocation rationale

The existing implementation allocates a screen-sized pixel buffer via capabilities-based allocation; this change removes that pattern.

- `heap_caps_aligned_alloc()` is a standard ESP-IDF heap API (ESP-IDF v5.5.2 docs):
  - https://docs.espressif.com/projects/esp-idf/en/v5.5.2/esp32p4/api-reference/system/mem_alloc.html
