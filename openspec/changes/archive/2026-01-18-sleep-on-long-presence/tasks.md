# Implementation Tasks

## 1. Add configuration
- [x] 1.1 Add `CONFIG_THEO_BACKLIGHT_PRESENCE_MAX_SECONDS` to `main/Kconfig.projbuild` under the existing "Radar Presence Sensor" menu, immediately after `CONFIG_THEO_RADAR_FAIL_THRESHOLD`.
- [x] 1.2 Use `int` type, range `60 3600`, default `60`.
- [x] 1.3 Add help text: "Maximum continuous presence-hold duration before forcing the backlight to sleep and setting presence_ignored."
- [x] 1.4 Add `CONFIG_THEO_BACKLIGHT_PRESENCE_MAX_SECONDS=60` to `sdkconfig.defaults` in the Backlight section near `CONFIG_THEO_BACKLIGHT_TIMEOUT_SECONDS`.

## 2. Backlight state additions
- [x] 2.1 In `main/thermostat/backlight_manager.c`, extend `backlight_state_t` (near the presence fields at lines ~60-64) with:
  - `int64_t presence_hold_start_us;`
  - `bool presence_hold_active;`
- [x] 2.2 No explicit init required; `memset(&s_state, 0, sizeof(s_state))` already zeros them.

## 3. Reset on non-presence interactions
- [x] 3.1 In `backlight_manager_notify_interaction()` (around line ~184), clear the presence-hold timer when the reason is:
  - `BACKLIGHT_WAKE_REASON_TOUCH`
  - `BACKLIGHT_WAKE_REASON_REMOTE`
  - `BACKLIGHT_WAKE_REASON_BOOT`
- [x] 3.2 Do **not** clear the timer for `BACKLIGHT_WAKE_REASON_PRESENCE`.
- [x] 3.3 Place this reset before `schedule_idle_timer()` so the interaction restarts the hold window immediately.

## 4. Track and enforce the cap in `presence_timer_cb()`
- [x] 4.1 After `now_us = esp_timer_get_time();`, start the hold timer when:
  - `radar_state.presence_detected` is true **and**
  - `s_state.backlight_lit` is true.
- [x] 4.2 If `presence_hold_active` is false, set it true and store `presence_hold_start_us = now_us`.
- [x] 4.3 If `presence_hold_active` is true, compute `hold_us = now_us - presence_hold_start_us` and compare against `SEC_TO_US(CONFIG_THEO_BACKLIGHT_PRESENCE_MAX_SECONDS)`.
- [x] 4.4 When the cap is exceeded:
  - Log an INFO line with the elapsed seconds.
  - Set `s_state.presence_ignored = true`.
  - Clear `presence_hold_active`, `presence_hold_start_us`, `presence_holding`, and `presence_wake_pending`.
  - Call `enter_idle_state()` and `return` from the callback.
- [x] 4.5 When presence is lost (`radar_state.presence_detected == false`), clear `presence_hold_active` and `presence_hold_start_us` alongside the existing presence resets.
- [x] 4.6 In the early-return branches for antiburn and radar offline, also clear `presence_hold_active` and `presence_hold_start_us` to avoid stale timers.

## 5. Logging expectations
- [x] 5.1 Use `ESP_LOGI(TAG, "[presence] hold exceeded; forcing idle (%" PRId64 "s)", seconds)` or an equivalent INFO log when the cap triggers.
- [x] 5.2 Keep existing WARN/INFO levels unchanged elsewhere.

## 6. Manual test plan update
- [x] 6.1 Append a new section in `docs/manual-test-plan.md` titled “Presence Hold Cap”.
- [x] 6.2 Add step-by-step validation:
  - Set `CONFIG_THEO_BACKLIGHT_PRESENCE_MAX_SECONDS=60` for a quicker run.
  - Let the display sleep, then approach within wake distance and confirm presence wake.
  - Stay in place and wait for the cap; confirm log shows the hold exceeded message and the screen turns off.
  - Remain present and verify the screen stays off (presence ignored).
  - Step away until presence clears, then re-approach and confirm presence wake works again.
  - Repeat with a touch interaction ~40 seconds in to confirm the cap timer resets and extends the on duration.

## 7. Build and on-device validation
- [x] 7.1 Run `idf.py build`.
- [x] 7.2 Flash and verify the presence-cap behavior plus the reset-on-touch/remote behavior.
