## 1. Update Constants

- [x] 1.1 Change `BACKLIGHT_FADE_MS` to 500 in `main/thermostat/backlight_manager.c`.

## 2. Centralize Hardware Control

- [x] 2.1 Update `set_brightness_immediate` in `backlight_manager.c` to handle the 0% case:
    - If `percent == 0`, call `bsp_display_backlight_off()` and set `s_state.backlight_lit = false`.
    - Else, call `bsp_display_brightness_set(percent)` and set `s_state.backlight_lit = true`.

## 3. Enable Bi-directional Fading

- [x] 3.1 Locate `start_backlight_fade` in `backlight_manager.c`.
- [x] 3.2 Remove the `if (target_percent <= start_percent)` guard that returns early with `set_brightness_immediate`.
- [x] 3.3 Ensure `start_backlight_fade` does NOT set `s_state.backlight_lit = true` directly; let `handle_fade_step` do it via `set_brightness_immediate`.

## 4. Refactor Sleep Transitions

- [x] 4.1 Update `enter_idle_state` in `backlight_manager.c`:
    - Remove the direct call to `bsp_display_backlight_off()`.
    - Replace with `start_backlight_fade(0, "idle")`.
- [x] 4.2 Update `apply_current_brightness` in `backlight_manager.c`:
    - Remove the `if (percent <= 0)` block that calls `bsp_display_backlight_off()`.
    - Allow the function to fall through to `start_backlight_fade(percent, reason)`.

## 5. Validation & Cleanup

- [x] 5.1 Verify that manual sleep (via power button) now fades out over 500ms.
- [x] 5.2 Verify that touching the screen during a fade-out results in a smooth fade-back-in (the "rebound").
- [x] 5.3 Check `apply_current_brightness` during Day/Night transitions to ensure they are now smooth fades.
- [x] 5.4 Log verification results in `proposal.md`.

## 6. Fix Side Effects

- [x] 6.1 Exempt LED "off" transitions from quiet-hours gate in `thermostat_leds.c`.
- [x] 6.2 Fix direction-aware clamping in `backlight_manager.c` to enable downward fading.

## 7. Implement Asymmetric Easing

- [x] 7.1 Implement Quadratic Ease-In in `backlight_manager.c`:
    - Add `backlight_easing_t` enum and `apply_easing` helper.
    - Update `handle_fade_step` to apply easing to normalized progress.
    - Automatically select Ease-In for downward transitions.
- [x] 7.2 Implement Quadratic Ease-In in `thermostat_leds.c`:
    - Add `LED_EASING_EASE_IN` to `led_easing_t` and `apply_easing`.
    - Update `thermostat_leds_off_with_fade` and `thermostat_leds_off_with_fade_eased` to use `LED_EASING_EASE_IN`.
- [x] 7.3 Verify the "accelerating" feel of fade-outs on hardware.
