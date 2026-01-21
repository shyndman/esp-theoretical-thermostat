# Design: Unify Backlight Fade Behavior

The goal is to transition the backlight manager from a simple one-way fader to a robust symmetric fader that handles all transitions smoothly.

## Architecture

### State Machine Adjustments

The current state machine in `backlight_manager.c` assumes that entering "idle" state is a terminal action for the backlight power. We will modify this to treat "idle" as a target of 0% brightness, allowing the existing fading logic to handle the transition.

### Fading Engine & Flag Lifecycle

The existing `handle_fade_step` already calculates `delta = target_percent - start_percent`. It handles both positive and negative deltas mathematically, but it is constrained by guards in `start_backlight_fade`.

**Critical Implementation Rule:**
- **`s_state.backlight_lit`**: This flag must remain `true` during the entire duration of a fade-out. It only becomes `false` when the hardware is actually powered off (at the end of the fade). This ensures that if the fade is interrupted (e.g., by a touch), the next fade starts from the current intermediate brightness rather than snapping to zero.

### Hardware Integration

We will centralize hardware power management in `set_brightness_immediate`:
- If `percent > 0`: Call `bsp_display_brightness_set(percent)` and ensure `backlight_lit = true`.
- If `percent == 0`: Call `bsp_display_backlight_off()` and set `backlight_lit = false`.

This centralization ensures that whether we reach 0% via a 500ms fade or an immediate jump (if requested), the hardware state remains consistent.

## Transition Rebound

A key UX requirement is the "rebound." If a user touches the screen while it is fading to black:
1.  `backlight_manager_notify_interaction` is called.
2.  `exit_idle_state` is called.
3.  `apply_current_brightness` is called.
4.  `stop_backlight_fade` halts the downward fade.
5.  `start_backlight_fade(100, ...)` begins a new fade starting from the *current intermediate* brightness level (because `backlight_lit` was still true).

This ensures a smooth, non-jarring recovery from an accidental or late-timed sleep.

## Technical Tradeoffs

- **Power Consumption**: The backlight stays on slightly longer (~500ms) during idle transitions. Given the 30s timeout, this extra energy use is negligible compared to the improved user experience.
- **Responsiveness**: A 500ms wake is fast enough to feel responsive while still providing the intended sense of polish.
