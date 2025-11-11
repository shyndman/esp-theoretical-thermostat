# Repository Guidelines

## Project Structure & Module Organization
- Firmware entry point lives in `main/app_main.c`, with UI logic under `main/thermostat/` and connectivity helpers in `main/connectivity/`.
- LVGL assets are generated into `main/assets/` from sources in `assets/fonts/` (future image sources will live in `assets/images/`) and the resulting C files are committed; always regenerate them with the provided scripts before pushing.
- Board/worktree helpers and automation live in `scripts/`; `managed_components/` is vendor code pulled via `idf.py` and should remain untouched.

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
