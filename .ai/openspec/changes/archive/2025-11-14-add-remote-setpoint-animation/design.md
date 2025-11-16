# Design: Remote setpoint animation pipeline

## Event flow
1. `mqtt_dataplane` already parses `target_temp_low/high` and calls `thermostat_apply_remote_setpoint()` inside the LVGL lock. We will replace that with `thermostat_remote_setpoint_controller_request(cooling_value_c, heating_value_c)` so the controller always reasons about the pair of setpoints together. Each request records the destination cooling/heating floats, derived geometry, and whether `backlight_manager_notify_interaction(BACKLIGHT_WAKE_REASON_REMOTE)` actually consumed the wake.
2. The controller maintains a single in-flight “session” plus one latest-wins pending session. While the first session is in `WaitingForLight` or the 1 s pre-delay, any incoming payload simply overwrites its targets. Once animation begins, we still allow one pending session: each new payload replaces the pending session’s pair, and as soon as the active animation completes we immediately start animating the pending session with **no** additional pre-delay. This repeats until the pending slot is empty.

## Backlight coordination
- `backlight_manager_notify_interaction()` already wakes the panel and returns `true` if it consumed the wake. We cache that flag per request so we know whether to schedule the remote sleep later.
- The controller polls/backoff-waits until `backlight_manager_is_idle()` returns `false` *and* `backlight_manager` reports the backlight as lit (we can expose a `backlight_manager_is_lit()` helper or latch the log callback). Only then do we advance to the pre-animation delay.
- Delays (1 s before animation, 1 s after) should be implemented via LVGL timers or esp_timer callbacks that run on the same context orchestrating the animation to avoid blocking other LVGL work.

## Animation specifics
- We continue to use `thermostat_compute_state_from_temperature()` to derive the destination `track_y`, `track_height`, and label offsets for *both* targets. The controller runs simultaneous LVGL animations for:
  - `g_cooling_track` and `g_heating_track` Y + height
  - `g_cooling_container` and `g_heating_container` translate-Y offsets so the numerals ride with the tracks
  - Cooling/heating setpoint labels update their text every frame using the animator’s intermediate temperature so numbers always mirror the current bar position.
- Animations reuse a shared easing profile (LVGL `lv_anim_path_ease_in_out`) with a fixed duration of 1600 ms. Start values come from the current geometry, end values from the new pair. Right before starting we poke the backlight “activity” hook so idle timers stay reset.
- Once `lv_anim` reports completion (via `lv_anim_set_deleted_cb` or LVGL events), the controller starts the trailing 1 s hold timer. After that timer, if the original session’s wake had `wake_consumed == true` and no other interactions occurred, it calls `backlight_manager_schedule_remote_sleep()` so the display turns back off. If another interaction fires during the animation or hold, the controller cancels the auto-sleep handoff.

## Error handling and logging
- If LVGL lock acquisition fails, we log and drop the animation (same as current behavior). Requests that cannot obtain the lock should retry until they succeed or a timeout occurs so we do not desync the UI.
- All timers and animations should log phase transitions (wake wait, pre-delay start, animation start/complete, sleep schedule) to make manual validation easy.
