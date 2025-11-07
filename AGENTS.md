# Repository Guidelines

## Project Structure & Module Organization
- `esp_lcd_hx8394.c` implements the HX8394 panel driver; keep board-specific helpers static.
- `include/esp_lcd_hx8394.h` is the public API surface—document new structs and enums inline.
- `test_apps/` contains the Unity integration harness, `sdkconfig.defaults*`, and locked dependencies in `managed_components/`.
- `tmp_esphome_mipi_dsi/` holds the ESPHome MIPI-DSI component; mirror driver-level changes and keep configs synchronized.
- `idf_component.yml` tracks metadata, targets (`esp32p4`), and dependencies; increment the version with behavioral changes.

## Build, Test, and Development Commands
- `. $IDF_PATH/export.sh` once per shell to load ESP-IDF tools.
- `cd test_apps && idf.py set-target esp32p4` aligns the build with default configs.
- `idf.py build` compiles the harness and downloads managed components.
- `idf.py flash monitor -p /dev/ttyUSB0` flashes hardware and streams Unity logs; adjust the serial port as needed.
- `idf.py menuconfig` tweaks GPIOs or timings; persist critical settings back into `sdkconfig.defaults`.

## Coding Style & Naming Conventions
- Follow ESP-IDF C style: 4-space indent, K&R braces, ≤100-character lines.
- Prefix static helpers with `panel_hx8394_`/`hx8394_`; keep exported symbols under the `esp_lcd_` namespace.
- Use ESP-IDF error-handling macros (`ESP_RETURN_ON_FALSE`, `ESP_GOTO_ON_ERROR`) and `ESP_LOGx` with the local `TAG`.
- Define configuration constants as upper-case macros positioned near their usage.

## Testing Guidelines
- Tests reside in `test_apps/main/test_esp_lcd_hx8394.c` using Unity `TEST_CASE` tags like `[hx8394][draw_pattern]`.
- Extend existing `setUp`/`tearDown` leak checks when adding cases; prefer deterministic delays over busy-waits.
- Run `idf.py build flash monitor` on target hardware to execute the suite and review Unity output.
- Note panel lanes, voltages, and supply expectations in test comments for reproducibility.

## Commit & Pull Request Guidelines
- Write imperative commit titles (e.g., `Add HX8394 DPI fallback`) with body context when needed.
- Reference relevant issues, component versions, or hardware revisions affected by the change.
- PR descriptions should summarize impact, list validation commands, and attach monitor logs or visuals for display updates.
- Request review from an ESP-IDF maintainer, ensure CI passes, and bump `idf_component.yml` when changing public APIs or behavior.

## Hardware & Configuration Tips
- Defaults target ESP32-P4 with two MIPI lanes and a 2.5 V LDO on channel 3; adjust macros for other boards before flashing.
- Keep `managed_components/` aligned with `idf_component.yml`; rerun `idf.py reconfigure` after dependency edits.
- Guard optional features with `#if SOC_MIPI_DSI_SUPPORTED` to keep the component portable to non-MIPI targets.
