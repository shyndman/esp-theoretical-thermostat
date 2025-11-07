# Theoretical Thermostat → ESP-IDF Migration Plan

This document outlines the agreed steps for moving the desktop-focused **theoretical-thermostat** LVGL UI into the ESP-IDF-based **esp-theoretical-thermostat** project that already boots the Waveshare ESP32-P4 BSP and `esp_lv_adapter`.

1. **Normalize LVGL configuration** – Map the simulator’s `lv_conf.h` options onto `sdkconfig.defaults*`: keep necessary features (animations, observers, timers), disable costly ones (FreeType, PNG decoders) unless justified, enforce 16-bit color, enlarge LVGL heap/PSRAM allocations, and clean duplicate entries such as `CONFIG_LV_DRAW_SW_DRAW_UNIT_CNT`.
2. **Carve out a reusable UI module** – Extract `thermostat_ui_*`, view-model data, timers, and helpers from `theoretical-thermostat/src/main.c` into a new component under `esp-theoretical-thermostat/main` (e.g., `thermostat_ui.c/.h`). Remove desktop `main()`/SDL glue and expose an entry point like `thermostat_ui_attach(lv_display_t *)`.
3. **Replace the HAL layer** – Drop `sdl_hal_init`, pthread/usleep usage, and rely solely on `esp_lv_adapter`’s display/touch handles. Ensure all LVGL mutations within the UI happen inside `esp_lv_adapter_lock()` and delegate periodic work to LVGL timers instead of host threads.
4. **Port assets/images** – Copy the needed generated image descriptors (at minimum `sunny`, `room_default`, `power`, `snowflake`, `fire`, `fan` frames) into the ESP project, re-running the `assets/images/manifest.yaml` pipeline for RGB565/alpha-friendly outputs and pruning unused frames if flash or RAM becomes tight.
5. **Convert fonts for MCU use** – Because on-device FreeType is disabled, bake the required `Figtree` weights/sizes into `.c` fonts via `lv_font_conv` (or LVGL’s converter), include them as `extern lv_font_t …`, and update `thermostat_fonts_init()` to use the compiled fonts instead of file paths.
6. **Scale/layout tuning** – Verify `g_layout_scale = ver_res / THERMOSTAT_TRACK_PANEL_HEIGHT` on the ESP32-P4 panel (portrait vs. landscape). If the board rotates the panel, introduce constants or Kconfig toggles so slider math (`THERMOSTAT_TRACK_TOP_Y`, etc.) adapts, and confirm touch coordinate mapping matches the BSP’s rotation.
7. **Data/timer abstraction** – Keep the randomized data timers initially but wrap them so future sensors can feed real data (callbacks, queues). Replace `rand()` with `esp_random()` or ESP-IDF RNG, and ensure timers rely solely on LVGL APIs to avoid non-RTOS dependencies.
8. **Integrate into `app_main.c`** – After `esp_lv_adapter_start()`, lock LVGL, invoke `thermostat_ui_attach()`, unlock, and drop the temporary “Hello LVGL!” label. Keep ongoing UI updates inside LVGL timers or FreeRTOS tasks that respect the adapter lock.
9. **Build/test pipeline** – Update `main/CMakeLists.txt` to compile new UI/asset sources, adjust `idf_component.yml` if helper libs are added, then validate both (a) simulator build to keep a desktop reference and (b) `idf.py build flash monitor` on ESP32-P4. Monitor heap usage and FPS, iterating on `sdkconfig.defaults` as needed.

## LVGL Configuration Baseline (Nov 7, 2025)

- `sdkconfig.defaults` now pins LVGL’s global knobs (`CONFIG_LV_CONF_SKIP`, 16-bit color depth, Flex/Grid/Observer, logging hooks) so IDF defconfigs match the simulator’s needs without relying on `lv_conf.h`.
- Heavy optional payloads (examples/demos and runtime image decoders such as PNG/BMP/TJPGD/FreeType) are explicitly disabled to keep flash and heap budgets focused on the thermostat UI.
- `sdkconfig.defaults.esp32p4` increases `CONFIG_LV_MEM_SIZE_KILOBYTES` to 256 and standardizes on two software draw units, leveraging PSRAM and the ESP32-P4’s extra core for better frame pacing while removing conflicting draw-unit settings.
