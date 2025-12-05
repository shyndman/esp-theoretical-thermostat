# Repository Guidelines

## Project Structure & Module Organization
1. `main/app_main.c` orchestrates boot: esp-hosted SDIO link (`esp_hosted_link_*`), Wi-Fi via `wifi_remote_manager`, SNTP sync (`time_sync_*`), MQTT (`mqtt_manager` + `mqtt_dataplane`), LVGL bring-up, `thermostat_ui_attach()`, backlight manager, and boot audio.
2. `main/thermostat/` holds UI code: `ui_state.h` defines the VM, `ui_theme.c` styles, `ui_top_bar.c` weather/room/hvac widgets, `ui_setpoint_view.c` and `ui_setpoint_input.c` render and capture slider gestures, `ui_actions.c` maps UI events to MQTT, `backlight_manager.c` tracks interaction wakeups, `audio_boot.c` streams embedded PCM.
3. `main/connectivity/` encapsulates transport: `esp_hosted_link.c` configures SDIO, `wifi_remote_manager.c` proxies ESP-Hosted Wi-Fi, `time_sync.c` wraps SNTP wait helpers, `mqtt_manager.c` negotiates the WebSocket client, and `mqtt_dataplane.c` handles thermostat payloads.
4. `main/assets/` contains committed outputs (fonts/images/audio) referenced directly by the firmware; `assets/fonts/fontgen.toml` and `assets/audio/soundgen.toml` are the sources regenerated via `scripts/generate_fonts.py` and `scripts/generate_sounds.py`.
5. `scripts/` includes `generate_fonts.py`, `generate_sounds.py`, and `init-worktree.sh` (ensures `sdkconfig` via `idf.py reconfigure` and `pre-commit setup-managed-symlinks`).
6. `docs/manual-test-plan.md` currently documents dataplane/MQTT validation steps; append additional scenarios there when you exercise other features.
7. `main/idf_component.yml` pins component dependencies (LVGL 9.4, esp_lvgl_adapter, esp_wifi_remote, esp_hosted, MQTT, FireBeetle 2 ESP32-P4 support, and the optional Waveshare Nano BSP); keep it aligned with `dependencies.lock`.

## Hardware Reality
- The primary hardware is the DFRobot FireBeetle 2 ESP32-P4 harness with the discrete MAX98357 path; the prior Waveshare ESP32-P4 Nano w/ES8311 codec is still supported but treated as legacy.
- The firmware targets a single, already-built thermostat unit. All UI layout work must assume one fixed display size/resolution; there is no notion of multiple devices or dynamic screen scaling.

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

## Configuration Tips
- Keep secrets out of `sdkconfig` by using environment overrides; never commit Wi-Fi credentials.
- If `idf.py` complains about the toolchain, resync your ESP-IDF checkout and rerun `scripts/init-worktree.sh` to regenerate `sdkconfig` scaffolding.

## Code Review Guidelines
1. Treat `openspec/changes/{name-of-this-pr's-change}/**` as the source of truth for every change. `{name-of-this-pr's-change}` usually matches the branch name or the change name called out in the PR description, so use that to locate the correct spec.
2. Call out any implementation that diverges from the referenced spec. Deviations are prohibited and treated as YAGNI, and are not to be merged with main.
3. If the spec is unclear or contradictory, block approval until the ambiguity is resolved (either by updating the spec or revising the code) and document the outcome in the review.

## Current Proposal
If we have a branch other than main checked out, its name often corresponds to the current OpenSpec change we're working on. 

<!-- OPENSPEC:START -->
# OpenSpec Instructions

These instructions are for AI assistants working in this project.

Always open `@/openspec/AGENTS.md` when the request:
- Mentions planning or proposals (words like proposal, spec, change, plan)
- Introduces new capabilities, breaking changes, architecture shifts, or big performance/security work
- Sounds ambiguous and you need the authoritative spec before coding

Use `@/openspec/AGENTS.md` to learn:
- How to create and apply change proposals
- Spec format and conventions
- Project structure and guidelines

Keep this managed block so 'openspec update' can refresh the instructions.

<!-- OPENSPEC:END -->


