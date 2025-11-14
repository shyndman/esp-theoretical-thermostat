# Repository Guidelines

## Project Structure & Module Organization
1. `main/app_main.c` orchestrates boot: esp-hosted SDIO link (`esp_hosted_link_*`), Wi-Fi via `wifi_remote_manager`, SNTP sync (`time_sync_*`), MQTT (`mqtt_manager` + `mqtt_dataplane`), LVGL bring-up, `thermostat_ui_attach()`, backlight manager, and boot audio.
2. `main/thermostat/` holds UI code: `ui_state.h` defines the VM, `ui_theme.c` styles, `ui_top_bar.c` weather/room/hvac widgets, `ui_setpoint_view.c` and `ui_setpoint_input.c` render and capture slider gestures, `ui_actions.c` maps UI events to MQTT, `backlight_manager.c` tracks interaction wakeups, `audio_boot.c` streams embedded PCM.
3. `main/connectivity/` encapsulates transport: `esp_hosted_link.c` configures SDIO, `wifi_remote_manager.c` proxies ESP-Hosted Wi-Fi, `time_sync.c` wraps SNTP wait helpers, `mqtt_manager.c` negotiates the WebSocket client, and `mqtt_dataplane.c` handles thermostat payloads.
4. `main/assets/` contains committed outputs (fonts/images/audio) referenced directly by the firmware; `assets/fonts/fontgen.toml` and `assets/audio/soundgen.toml` are the sources regenerated via `scripts/generate_fonts.py` and `scripts/generate_sounds.py`.
5. `scripts/` includes `generate_fonts.py`, `generate_sounds.py`, and `init-worktree.sh` (ensures `sdkconfig` via `idf.py reconfigure` and `pre-commit setup-managed-symlinks`).
6. `docs/manual-test-plan.md` currently documents dataplane/MQTT validation steps; append additional scenarios there when you exercise other features.
7. `main/idf_component.yml` pins component dependencies (LVGL 9.4, esp_lvgl_adapter, esp_wifi_remote, esp_hosted, MQTT, Waveshare board support); keep it aligned with `dependencies.lock`.

## Build, Test, and Development Commands
- `idf.py build` — configures CMake, builds the ESP32-P4 target, and emits binaries under `build/`.
- `idf.py -p <PORT> flash monitor` — flashes the board and tails logs; use the correct USB device for `<PORT>`.
- `scripts/generate_fonts.py` — regenerates LVGL font blobs from `assets/fonts/fontgen.toml`; run before committing asset changes.
- `scripts/init-worktree.sh` — replays IDF configuration when creating a new git worktree (requires `IDF_PATH`).

## Coding Style & Naming Conventions
- C files use 2-space indentation, no tabs; wrap at ~100 columns and prefer early returns for error paths.
- Follow ESP-IDF logging macros (`ESP_LOGI/W/E`) with consistent `TAG` strings; configs come from `sdkconfig`/`sdkconfig.defaults`.
- Namespaces are folder-based: `thermostat_*` for UI, `wifi_remote_*` for connectivity. Keep new files aligned with this pattern.

## Testing Guidelines
- No dedicated unit-test harness exists; rely on `idf.py build` plus on-device validation. Keep manual test notes in PRs.
- When adding UI logic, verify LVGL interactions under `esp_lv_adapter_lock()` to avoid race conditions.
- Add WARN-level logs for unimplemented runtime branches so they surface during hardware runs.

## Commit & Pull Request Guidelines
- Match existing history: short (<72 char) imperative subject lines (`"Implement anti-burn"`, `"Wire up pre-commit"`).
- One logical change per commit; mention relevant `sdkconfig` deltas explicitly.
- PRs should include: 1) summary of behavior change, 2) hardware/test evidence (log excerpt or video), 3) linked issue or rationale, 4) screenshots for UI tweaks.

## Security & Configuration Tips
- Keep secrets out of `sdkconfig` by using environment overrides; never commit Wi-Fi credentials.
- If `idf.py` complains about the toolchain, resync your ESP-IDF checkout and rerun `scripts/init-worktree.sh` to regenerate `sdkconfig` scaffolding.
