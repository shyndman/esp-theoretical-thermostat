# Theoretical Thermostat UI

## Overview
This project builds an ESP32-P4 powered thermostat UI that speaks MQTT over WebSockets to Home Assistant. The firmware boots an esp-hosted SDIO link, brings up Wi-Fi through esp_wifi_remote, syncs time, starts an LVGL-based UI, and plays a boot chime once the backlight manager marks the UI ready.

## Hardware & Software
- DFRobot FireBeetle 2 ESP32-P4 dev board (flash via Espressif's `ESP32P4 Dev Module` profile). The earlier Waveshare ESP32-P4 Nano remains supported when you select its BSP codec pipeline.
- ESP-Hosted SDIO co-processor and esp_wifi_remote host firmware.
- LVGL 9.4 with `esp_lvgl_adapter` for display + GT911 touch integration.
- Host tooling: ESP-IDF (matching `idf.py` target) with `IDF_PATH` configured; `lv_font_conv` or `npx` for font generation; Python 3.11 with `uv` for scripts.
- (Thick) 3D printed case. [CAD available through OnShape](https://cad.onshape.com/documents/2585d541ab6cc819f7411bef/w/9246e9356192ec1c78b324c3/e/e9b7b173b333467071f18b2f?renderMode=0&uiState=69172ea5ff4f650b7432759b)

## Audio Pipelines
1. Select the backend via `idf.py menuconfig → Thermostat Connectivity → Audio Cues → Audio output pipeline`. The FireBeetle 2 harness defaults to `MAX98357 I2S amp`; pick `NANO BSP codec` only when you are flashing a Waveshare ESP32-P4 Nano and need its onboard ES8311.
2. When `MAX98357 I2S amp` is active, wire LRCLK to GPIO23, BCLK to GPIO22, DIN to GPIO21, and optionally drive the amp's SD/EN pin from GPIO20 (set `CONFIG_THEO_AUDIO_I2S_ENABLE_GPIO=-1` if the pin stays tied high). Volume still flows through `CONFIG_THEO_BOOT_CHIME_VOLUME` and the shared audio policy.
3. The MAX path relies on 16 kHz mono PCM; ensure the amp shares ground with the ESP32-P4 board and keep the speaker enable line low during boot if you need silence until firmware raises it.
4. The `NANO BSP codec` path still targets the Waveshare slot: no wiring changes, just the BSP's ES8311 speaker codec and the same audio policy.

## Build & Flash
1. `idf.py build` — configures and builds the ESP32-P4 target, emitting binaries under `build/`.
2. `idf.py -p <PORT> flash monitor` — flash over USB and tail logs (replace `<PORT>` with the board’s serial device)

## Assets
- Fonts: `scripts/generate_fonts.py` reads `assets/fonts/fontgen.toml` and emits C blobs into `main/assets/fonts/`; requires `lv_font_conv` or `npx`.
- Audio: `scripts/generate_sounds.py` reads `assets/audio/soundgen.toml` and produces PCM arrays under `main/assets/audio/`; validates channel/bit-depth/sample-rate metadata.
- Images: pre-generated LVGL assets live under `main/assets/images/` and are compiled directly; regenerate via future image tooling when sources land in `assets/images/`.

## Repository Map
- `main/app_main.c` — boot orchestration: esp_hosted_link, wifi_remote_manager, time_sync, mqtt_manager/mqtt_dataplane, LVGL bring-up, `thermostat_ui_attach()`, `backlight_manager`, boot audio.
- `main/thermostat/` — LVGL UI implementation: `ui_state.h`, `ui_theme.c`, `ui_top_bar.c`, `ui_setpoint_view.c`, `ui_setpoint_input.c`, `ui_actions.c`, `backlight_manager.c`, `audio_boot.c`.
- `main/connectivity/` — transport helpers: `esp_hosted_link.c`, `wifi_remote_manager.c`, `time_sync.c`, `mqtt_manager.c`, `mqtt_dataplane.c`.
- `main/assets/` — committed font/image/audio artifacts consumed by the firmware.
- `scripts/` — asset generators and `init-worktree.sh` bootstrapping helper.
- `docs/manual-test-plan.md` — current manual validation steps (MQTT dataplane focus).

## Validation
- esp-idf testing is in its early days; rely on `idf.py build` plus on-device testing.
- Follow `docs/manual-test-plan.md` to verify MQTT dataplane behavior: setpoint slider publishes, MQTT subscriptions arrive, and UI reflects inbound payloads without rollback.
- Log WARN-level messages when branches remain unimplemented so hardware runs surface gaps quickly.

## Component Dependencies
- Managed via `main/idf_component.yml` (LVGL 9.4, esp_lvgl_adapter beta, esp_wifi_remote, esp_hosted 2.6.4, espressif/mqtt) with hashes tracked in `dependencies.lock`; update both when bumping versions.
