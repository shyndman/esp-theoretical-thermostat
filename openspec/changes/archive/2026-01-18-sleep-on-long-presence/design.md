# Design: Sleep on Long Presence

## Context
Presence detection currently keeps the backlight awake indefinitely as long as the radar reports any target. That behavior lives in `main/thermostat/backlight_manager.c` where the presence timer sets `presence_holding = true` on every presence frame and the idle timer defers sleep (see `presence_timer_cb()` around `backlight_manager.c:349` and `idle_timer_cb()` around `backlight_manager.c:313`). We need a configurable cap that forces sleep after 5 minutes of continuous presence, then ignores presence until the radar reports no target.

## Current Architecture (Verified)
1) `backlight_state_t` keeps presence-related state: `presence_ignored`, `presence_wake_pending`, `presence_first_close_us`, and `presence_holding` (`backlight_manager.c:39-64`).
2) `presence_timer_cb()` polls the radar via `radar_presence_get_state()`, performs the dwell check, wakes the backlight via `exit_idle_state("presence")`, and sets `presence_holding = true` whenever `radar_state.presence_detected` is true (`backlight_manager.c:349-421`).
3) `backlight_manager_notify_interaction()` is the central non-presence wake hook called by:
   1. Boot (`backlight_manager_on_ui_ready()` calls `BACKLIGHT_WAKE_REASON_BOOT`, `backlight_manager.c:174-181`).
   2. Touch (`main/thermostat_ui.c:166-180`, `main/thermostat/ui_actions.c:99-147`, `main/thermostat/ui_setpoint_input.c:101-115`).
   3. Remote (`main/thermostat/remote_setpoint_controller.c:171-179`).
4) Manual sleep uses `backlight_manager_request_sleep()` which sets `presence_ignored = true` and calls `enter_idle_state()` (`backlight_manager.c:298-305`).

## Goals / Non-Goals
**Goals:**
1) Enforce a configurable maximum continuous presence-hold duration (default 300 seconds) before forcing idle sleep.
2) Keep the cap tied to continuous presence only; reset the timer on any non-presence interaction (touch/remote/boot).
3) Preserve existing wake-on-presence dwell logic and `presence_ignored` semantics.

**Non-Goals:**
1) Changing radar polling intervals, dwell thresholds, or wake distance logic.
2) Altering the remote sleep flow or antiburn behavior.
3) Introducing new UI elements or settings.

## Decisions

### Decision 1: Track hold timing inside `backlight_state_t`
We’ll add new fields to `backlight_state_t` so the timing is managed by the same module that owns `presence_holding`.

**Proposed fields:**
1) `int64_t presence_hold_start_us` – the start time of the current continuous hold.
2) `bool presence_hold_active` – whether we’ve started a continuous hold.

**Rationale:**
1) The state already owns presence-related flags, so this keeps logic co-located.
2) `esp_timer_get_time()` is already used in `presence_timer_cb()` for dwell timing, so we can reuse it for hold timing.

### Decision 2: Start timing only when the backlight is lit
We start the hold timer when `radar_state.presence_detected` is true **and** `s_state.backlight_lit` is true. That includes the moment a presence wake calls `exit_idle_state("presence")` and the backlight lights.

**Rationale:**
1) The cap is meant to limit “light on due to presence,” not to run while the display is already off.
2) It aligns with the requirement “when the backlight is on as a result of presence detection.”

### Decision 3: Reset on non-presence interactions via `backlight_manager_notify_interaction()`
We reset the hold timer when `backlight_manager_notify_interaction()` is called with `BACKLIGHT_WAKE_REASON_TOUCH`, `BACKLIGHT_WAKE_REASON_REMOTE`, or `BACKLIGHT_WAKE_REASON_BOOT`.

**Rationale:**
1) These are the only current non-presence interaction sources; the enum value `BACKLIGHT_WAKE_REASON_TIMER` exists but has no call sites.
2) Keeps logic centralized and consistent with existing wake and idle scheduling behavior.

### Decision 4: Force sleep + ignore until absence when the cap is exceeded
When the cap expires, the backlight manager should:
1) Log an INFO line indicating the cap fired and how long the hold lasted.
2) Set `presence_ignored = true` so presence wakes are suppressed.
3) Clear `presence_holding` and any hold timing fields.
4) Call `enter_idle_state()` to immediately turn off the backlight.

**Rationale:**
1) This mirrors the manual sleep behavior: `presence_ignored` is already used to suppress immediate wake while a user is still present (`backlight_manager.c:304-305`).
2) Keeping the backlight off until radar reports no presence matches the existing “presence ignored clears on absence” flow (`backlight_manager.c:429-432`).

## Configuration
Add a new Kconfig option under the existing “Radar Presence Sensor” menu in `main/Kconfig.projbuild`:
1) `CONFIG_THEO_BACKLIGHT_PRESENCE_MAX_SECONDS`
2) Default: 300
3) Range: 60–3600
4) Help text: “Maximum continuous presence-hold duration before forcing the backlight to sleep and setting presence_ignored.”

Also add `CONFIG_THEO_BACKLIGHT_PRESENCE_MAX_SECONDS=300` to `sdkconfig.defaults` in the Backlight section.

## Dependency Verification

### cosmavergari/ld2410 (ESP Component)
1) The project pulls `cosmavergari/ld2410` via git in `main/idf_component.yml` and pins it to commit `87255ac028f2cc94ba6ee17c9df974f39ebf7c7e` in `dependencies.lock`.
2) The upstream component manifest (`idf_component.yml`) declares version `0.0.2`, and the latest `main` commit matches the pinned hash.
3) The public ESP Component Registry does not list this component; it is pulled directly from GitHub.
4) The component’s `Kconfig.projbuild` defines the UART options we reference:
   1. `LD2410_UART_PORT_NUM` (default 2, range 0–2 for ESP32/ESP32S3, otherwise 0–1)
   2. `LD2410_UART_TX` (default 13)
   3. `LD2410_UART_RX` (default 14)
   4. `LD2410_UART_BAUD_RATE` (default 256000)
5) Our firmware overrides these defaults via `sdkconfig.defaults` as called out in the spec delta.

### ESP-IDF `esp_timer` (v5.5.2)
1) The API is provided by `components/esp_timer/include/esp_timer.h` and included with `#include "esp_timer.h"`.
2) `int64_t esp_timer_get_time(void)` returns microseconds since ESP Timer initialization, which occurs before `app_main`.
3) `esp_timer_start_once(esp_timer_handle_t timer, uint64_t timeout_us)` requires the timer to be stopped before starting; restart/stop first if needed.
4) `esp_timer_get_time()` is documented as safe to call from tasks or ISR contexts, so calling it inside `presence_timer_cb()` is valid.

## Implementation Guide

### 1) State additions
Add fields in `backlight_state_t` near the existing presence fields (`backlight_manager.c:60-64`):
1) `int64_t presence_hold_start_us;`
2) `bool presence_hold_active;`

Initialize them to zero/false alongside `memset(&s_state, 0, sizeof(s_state));` (no special init required).

### 2) Reset logic on non-presence interactions
In `backlight_manager_notify_interaction()` (`backlight_manager.c:184-209`):
1) If `reason` is `BACKLIGHT_WAKE_REASON_TOUCH`, `BACKLIGHT_WAKE_REASON_REMOTE`, or `BACKLIGHT_WAKE_REASON_BOOT`, clear `presence_hold_active` and `presence_hold_start_us`.
2) Do not clear these fields for `BACKLIGHT_WAKE_REASON_PRESENCE`.

**Pseudocode:**
```c
if (reason == BACKLIGHT_WAKE_REASON_TOUCH ||
    reason == BACKLIGHT_WAKE_REASON_REMOTE ||
    reason == BACKLIGHT_WAKE_REASON_BOOT) {
    s_state.presence_hold_active = false;
    s_state.presence_hold_start_us = 0;
}
```

### 3) Hold timing + cap enforcement in `presence_timer_cb()`
After `now_us` is computed (`backlight_manager.c:383`), extend the presence branch:
1) If `radar_state.presence_detected` is true **and** `s_state.backlight_lit` is true:
   1. If `presence_hold_active` is false, set it true and set `presence_hold_start_us = now_us`.
   2. Else compute `hold_us = now_us - presence_hold_start_us`.
   3. If `hold_us` exceeds `SEC_TO_US(CONFIG_THEO_BACKLIGHT_PRESENCE_MAX_SECONDS)`, trigger the cap.
2) When the cap triggers:
   1. Log: `ESP_LOGI(TAG, "[presence] hold exceeded; forcing idle (%" PRId64 "s)", hold_us / 1000000);`
   2. Set `presence_ignored = true`.
   3. Clear `presence_hold_active` and `presence_hold_start_us`.
   4. Clear `presence_holding` and `presence_wake_pending` (to avoid immediate re-arm).
   5. Call `enter_idle_state()` and return early from `presence_timer_cb()`.
3) If presence is lost (`radar_state.presence_detected == false`), clear `presence_hold_active` and `presence_hold_start_us` along with existing resets.

**Pseudocode:**
```c
if (radar_state.presence_detected) {
    if (s_state.backlight_lit) {
        if (!s_state.presence_hold_active) {
            s_state.presence_hold_active = true;
            s_state.presence_hold_start_us = now_us;
        } else {
            int64_t hold_us = now_us - s_state.presence_hold_start_us;
            if (hold_us >= SEC_TO_US(CONFIG_THEO_BACKLIGHT_PRESENCE_MAX_SECONDS)) {
                ESP_LOGI(TAG, "[presence] hold exceeded; forcing idle (%" PRId64 "s)",
                         hold_us / 1000000);
                s_state.presence_ignored = true;
                s_state.presence_hold_active = false;
                s_state.presence_hold_start_us = 0;
                s_state.presence_holding = false;
                s_state.presence_wake_pending = false;
                enter_idle_state();
                return;
            }
        }
    }
    s_state.presence_holding = true;
} else {
    s_state.presence_hold_active = false;
    s_state.presence_hold_start_us = 0;
    // existing presence reset logic continues
}
```

### 4) Logging expectations
1) Use `ESP_LOGI` for the hold-cap trigger so it’s visible in normal logs.
2) Keep the tag `TAG` (“backlight”) to match existing logging style.
3) Avoid WARN unless the logic is failing; this is normal behavior.

## Risks / Trade-offs
1) **Risk:** The cap could interrupt a user who is actively using the device but not touching it.
   - **Mitigation:** Reset on touch or remote interaction, so any explicit activity restarts the hold.
2) **Risk:** Cap fires while the display is already off.
   - **Mitigation:** Only start the hold timer when `s_state.backlight_lit` is true.

## Migration Plan
1) None. The change is additive and only impacts behavior when presence holds exceed the configured cap.

## Open Questions
1) None. The behavior, reset conditions, and cap enforcement are now fully specified.
