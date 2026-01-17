# Change: Sleep on long presence

## Why
Presence-based wakes can keep the display lit indefinitely. Adding a max hold duration avoids unnecessary backlight wear and power draw while preserving wake-on-presence behavior.

## What Changes
- Add a configurable max presence-hold duration (default 5 minutes) that forces idle sleep and sets `presence_ignored` until presence clears.
- Reset the presence hold timer on non-presence `backlight_manager_notify_interaction()` calls (touch, remote, or boot).
- Add `CONFIG_THEO_BACKLIGHT_PRESENCE_MAX_SECONDS` to Kconfig and `sdkconfig.defaults`.
- Update backlight manager logic to track continuous presence and enforce the cap.

## Implementation Notes
1) Add `presence_hold_start_us` and `presence_hold_active` to `backlight_state_t` in `main/thermostat/backlight_manager.c` to track continuous presence while the backlight is lit.
2) Start timing inside `presence_timer_cb()` once `radar_state.presence_detected` is true and `s_state.backlight_lit` is true; reset timing when presence is lost.
3) Enforce the cap in `presence_timer_cb()` by logging an INFO line, setting `presence_ignored = true`, clearing hold state, and calling `enter_idle_state()` when the duration exceeds `CONFIG_THEO_BACKLIGHT_PRESENCE_MAX_SECONDS`.
4) Reset the hold timer in `backlight_manager_notify_interaction()` only for `BACKLIGHT_WAKE_REASON_TOUCH`, `BACKLIGHT_WAKE_REASON_REMOTE`, and `BACKLIGHT_WAKE_REASON_BOOT`.
5) Add the Kconfig option under the existing “Radar Presence Sensor” menu and keep the default in `sdkconfig.defaults` under the Backlight section.

## Dependency Verification
1) `cosmavergari/ld2410` is sourced via git (not the public registry) and pinned to commit `87255ac028f2cc94ba6ee17c9df974f39ebf7c7e` (upstream version `0.0.2`).
2) The LD2410 Kconfig options in the spec match the upstream component’s `Kconfig.projbuild` (UART port, RX/TX pins, baud rate).
3) ESP-IDF `v5.5.2` confirms `esp_timer_get_time()` returns microseconds since init and is safe in timer callbacks.

## Impact
- Affected specs: radar-presence-sensing
- Affected code: `main/thermostat/backlight_manager.c`, `main/Kconfig.projbuild`, `sdkconfig.defaults`
