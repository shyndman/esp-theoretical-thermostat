# Repository Guidelines

## Project Structure & Module Organization
- `esp_lcd_hx8394.c` implements the HX8394 panel driver; keep board-specific helpers static.
- `include/esp_lcd_hx8394.h` is the public API surface—document new structs and enums inline.
- `main/app_main.c` hosts the BSP/LVGL demo entry point that flashes on hardware via `idf.py`; treat it as the staging area before the full thermostat UI is ported.
- `main/test_esp_lcd_hx8394.c` is the archived Unity/pattern reference; it is not part of the default component build but can be re-enabled for panel bring-up.
- `managed_components/` is owned by ESP-IDF’s component manager (`idf.py add-dependency`); never hand-edit anything inside.
- `idf_component.yml` tracks metadata, targets (`esp32p4`), and dependencies; increment the version with behavioral changes.

## Build, Test, and Development Commands
- `. $IDF_PATH/export.sh` once per shell to load ESP-IDF tools.
- `idf.py set-target esp32p4 && idf.py build` (repo root) compiles the barebones LVGL firmware in `main/app_main.c`; a cold build can take several minutes because LVGL, BSP, and panel components rebuild, so rerun only when necessary.
- Use incremental targets when iterating (`idf.py build app`, `idf.py flash`, or `idf.py -T main build`); they shorten loops when only `main/` changes.
- `idf.py flash monitor -p /dev/ttyUSB0` flashes hardware and streams logs; adjust the serial port as needed.
- `idf.py menuconfig` tweaks GPIOs or timings; persist critical settings back into `sdkconfig.defaults` and `sdkconfig.defaults.esp32p4`.

## Coding Style & Naming Conventions
- Follow ESP-IDF C style: 4-space indent, K&R braces, ≤100-character lines.
- Prefix static helpers with `panel_hx8394_`/`hx8394_`; keep exported symbols under the `esp_lcd_` namespace.
- Use ESP-IDF error-handling macros (`ESP_RETURN_ON_FALSE`, `ESP_GOTO_ON_ERROR`) and `ESP_LOGx` with the local `TAG`.
- Define configuration constants as upper-case macros positioned near their usage.

## Testing Guidelines
- To revive the HX8394 color/pattern diagnostics, temporarily add `main/test_esp_lcd_hx8394.c` back to `idf_component_register` and flash that build on hardware.
- Keep Unity `setUp`/`tearDown` leak checks intact when adding new cases; prefer deterministic delays over busy-waits.
- Always validate on the target with `idf.py build flash monitor` and capture panel lane/backlight details in test comments for reproducibility.

## Commit & Pull Request Guidelines
- Write imperative commit titles (e.g., `Add HX8394 DPI fallback`) with body context when needed.
- Reference relevant issues, component versions, or hardware revisions affected by the change.
- PR descriptions should summarize impact, list validation commands, and attach monitor logs or visuals for display updates.
- Request review from an ESP-IDF maintainer, ensure CI passes, and bump `idf_component.yml` when changing public APIs or behavior.

## Hardware & Configuration Tips
- Defaults target ESP32-P4 with two MIPI lanes and a 2.5 V LDO on channel 3; adjust macros for other boards before flashing.
- Keep `managed_components/` aligned with `idf_component.yml`; rerun `idf.py reconfigure` after dependency edits.
- Guard optional features with `#if SOC_MIPI_DSI_SUPPORTED` to keep the component portable to non-MIPI targets.
