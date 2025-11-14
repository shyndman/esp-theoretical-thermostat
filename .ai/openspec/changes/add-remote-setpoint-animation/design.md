# Design: Remote setpoint animation pipeline

## Event flow
1. `mqtt_dataplane` already parses `target_temp_low/high` and calls `thermostat_apply_remote_setpoint()` inside the LVGL lock. We will introduce `thermostat_remote_setpoint_controller_request(target, value_c)` as the sole entry point. This controller captures the parsed/clamped float, current slider geometry, and whether the backlight wake request actually powered the panel (the bool return from `backlight_manager_notify_interaction(BACKLIGHT_WAKE_REASON_REMOTE)`).
2. The controller enqueues requests so we never mutate LVGL from multiple MQTT messages concurrently. If a new payload arrives while an animation is running, it should either update the pending target (latest wins) or restart the sequence only after the previous animation finishes its post-delay.

## Backlight coordination
- `backlight_manager_notify_interaction()` already wakes the panel and returns `true` if it consumed the wake. We cache that flag per request so we know whether to schedule the remote sleep later.
- The controller polls/backoff-waits until `backlight_manager_is_idle()` returns `false` *and* `backlight_manager` reports the backlight as lit (we can expose a `backlight_manager_is_lit()` helper or latch the log callback). Only then do we advance to the pre-animation delay.
- Delays (1 s before animation, 1 s after) should be implemented via LVGL timers or esp_timer callbacks that run on the same context orchestrating the animation to avoid blocking other LVGL work.

## Animation specifics
- We continue to use `thermostat_compute_state_from_temperature()` to derive the destination `track_y`, `track_height`, and label offsets. The controller feeds these into LVGL animations for:
  - `g_cooling_track`/`g_heating_track` Y + height properties
  - `g_cooling_container`/`g_heating_container` translate-Y offsets so the big numerals slide smoothly
- Animations reuse a shared easing profile (LVGL `lv_anim_path_ease_in_out`) with a fixed duration of 1600 ms. Start values come from the current object state, end values from the new setpoint. Non-animated properties (text, color, active-target opacity) update right before the animation so numbers stay in sync.
- Once `lv_anim` reports completion (via `lv_anim_set_deleted_cb` or LVGL events), the controller starts the trailing 1 s hold timer. After that timer, if the original request had `wake_consumed == true` and no other interactions occurred, it calls `backlight_manager_schedule_remote_sleep()` so the display turns back off. If another interaction fires during the animation (e.g., touch), the controller cancels the auto-sleep handoff.

## Error handling and logging
- If LVGL lock acquisition fails, we log and drop the animation (same as current behavior). Requests that cannot obtain the lock should retry until they succeed or a timeout occurs so we do not desync the UI.
- All timers and animations should log phase transitions (wake wait, pre-delay start, animation start/complete, sleep schedule) to make manual validation easy.
