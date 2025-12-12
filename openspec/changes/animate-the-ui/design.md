# Design: Boot Transition and UI Animation System

## Context
The thermostat boot sequence currently runs two independent visual sequences:
1. LED ceremony in `thermostat_led_status.c`: sparkle drain → white fade-in (600ms via `thermostat_leds_solid_with_fade()`) → hold (1200ms timer) → black fade-out (2000ms via `thermostat_leds_off_with_fade_eased()`)
2. Splash screen in `ui_splash.c`: visible during boot, then 500ms fade-out via `lv_scr_load_anim()` with `LV_SCR_LOAD_ANIM_FADE_OUT`

The callback structure is:
- `thermostat_splash_destroy()` stores callback, flushes queue, calls `splash_start_exit_sequence()`
- `splash_start_exit_sequence()` invokes callback WITHOUT lock (to create main UI), then fades

Main UI elements "pop in" with no entrance animation. Active setpoint changes trigger instant color updates via `thermostat_setpoint_apply_active_styles()` in `ui_helpers.c`.

## Goals
- Create a cohesive, unified boot-to-main-UI transition
- LED and splash transitions synchronized as one ceremony
- Main UI elements animate in with orchestrated choreography
- Setpoint color transitions feel smooth during interaction
- All timing constants in one file for easy iteration

## Non-Goals
- Changing the sparkle animation behavior
- Modifying HVAC state LED effects
- Altering the boot sequence order or dependencies

## Decisions

### 1. Timing Constants File
**Decision:** Create `main/thermostat/ui_animation_timing.h` with two logical sections:
- Intro animation timings (LED + splash + UI entrance)
- Interaction animation timings (setpoint transitions)

**Rationale:** Single source of truth for all animation durations enables rapid iteration.

### 2. Synchronized Fade-Out Mechanism
**Decision:** Modify `thermostat_led_status.c` to signal when splash fade should begin. The splash fade will be triggered from the LED timer callback at `TIMER_STAGE_BOOT_HOLD` completion, ensuring both start simultaneously.

Current flow:
```
start_boot_success_sequence() → leds_solid_with_fade(600ms) → schedule BOOT_HOLD(1200ms)
BOOT_HOLD callback → leds_off_with_fade_eased(2000ms) → schedule BOOT_COMPLETE(2100ms)
```

New flow:
```
start_boot_success_sequence() → leds_solid_with_fade(1200ms) → schedule BOOT_HOLD(1200ms)
BOOT_HOLD callback → leds_off_with_fade_eased(2000ms) + trigger_splash_fade() → schedule BOOT_COMPLETE(2100ms)
```

### 3. Splash Fade Coordination
**Decision:** Add a new function `thermostat_splash_begin_fade()` that can be called from LED status to start the splash fade at the right moment. The splash will wait (remain visible) until this is called rather than fading immediately when `thermostat_splash_destroy()` is invoked.

**Files affected:**
- `ui_splash.c`: Add state to track "waiting for fade signal" vs "fading"
- `ui_splash.h`: Expose `thermostat_splash_begin_fade()`
- `thermostat_led_status.c`: Call splash fade from `TIMER_STAGE_BOOT_HOLD` callback

### 4. Main UI Entrance Animation Orchestration
**Decision:** Create new file `main/thermostat/ui_entrance_anim.c` to orchestrate entrance animations. This avoids polluting existing UI files with animation logic.

Elements to animate (all stored as static globals in their respective files):
- **Top bar** (`ui_top_bar.c`): `g_weather_group`, `g_hvac_status_group`, `g_room_group`
- **Tracks** (`ui_setpoint_view.c`): `g_cooling_track`, `g_heating_track`
- **Labels** (`ui_setpoint_view.c`): `g_cooling_label`, `g_cooling_fraction_label`, `g_heating_label`, `g_heating_fraction_label`
- **Action bar** (`ui_actions.c`): `g_mode_icon`, `g_power_icon`, `g_fan_icon`

**Accessor functions needed:** The static globals need getter functions exposed in headers, or the entrance animation module needs to coordinate via existing public APIs.

**Timeline (T=0 is 400ms before fade-out completes):**
- T=0: Top bar fades begin (Weather → HVAC → Room, 100ms stagger, 600ms each)
- T=0: Cooling track growth begins (1200ms, from height 0 to final)
- T=400: Heating track growth begins (1200ms, from height 0 to final)
- T=1200: Cooling labels fade in (whole: 400ms, fractional: +200ms delay)
- T=1600: Heating labels fade in (whole: 400ms, fractional: +200ms delay)
- T=2200: Action bar fades begin (Mode → Power → Fan, 100ms stagger, 600ms each)
- T=3000: Complete, touch unblocked

### 5. Touch Blocking
**Decision:** Add `g_entrance_anim_active` flag in `ui_entrance_anim.c`, exposed via getter. Modify touch handlers in `ui_setpoint_input.c` and `ui_actions.c` to check this flag.

Touch handlers already have a pattern for early-out:
```c
if (backlight_manager_notify_interaction(BACKLIGHT_WAKE_REASON_TOUCH)) {
  return;
}
```

Add similar check for entrance animation active.

### 6. Track Height Animation
**Decision:** Use LVGL's `lv_anim_t` to animate height. Tracks are positioned via `lv_obj_set_y()` and `lv_obj_set_height()`. For bottom-up growth:
- Keep Y at final position (bottom of track stays fixed)
- Animate height from 0 to final value
- This creates the "growing from bottom" effect

### 7. Setpoint Color Transition
**Decision:** Modify `thermostat_setpoint_apply_active_styles()` in `ui_helpers.c` to optionally animate color changes. Add an `animate` boolean parameter or detect if entrance is complete.

Use LVGL style animation for color transition - can animate `LV_STYLE_TEXT_COLOR` and `LV_STYLE_BG_COLOR` properties.

**Files affected:**
- `ui_helpers.c`: Add color animation logic to `thermostat_setpoint_apply_active_styles()`
- `ui_setpoint_input.c`: `thermostat_select_setpoint_target()` calls `thermostat_update_active_setpoint_styles()` which calls the helper

### 8. Initialization Order Consideration
**Decision:** UI elements must be created with initial hidden state (opacity 0 for fades, height 0 for tracks), then entrance animation makes them visible. This requires modifying creation functions or adding post-creation initialization.

Current creation in `thermostat_ui_init()`:
```c
thermostat_create_top_bar(g_root_screen);
thermostat_create_weather_group(top_bar);
thermostat_create_hvac_status_group(top_bar);
thermostat_create_room_group(top_bar);
thermostat_create_tracks(g_root_screen);
// ... etc
```

Add call to `thermostat_entrance_anim_prepare()` after creation to set initial hidden states, then `thermostat_entrance_anim_start()` triggered from splash fade callback.

## Risks / Trade-offs

| Risk | Mitigation |
|------|------------|
| Animation complexity increases perceived boot time | Entrance starts 400ms before fade completes, overlapping |
| LVGL animation queue congestion | Use dedicated animation variables per element |
| Static globals not accessible | Add getter functions to headers |
| Color animation may flicker | Test with actual hardware, adjust easing |

## Open Questions
None - all clarified during design discussion.

## Key Files to Modify

| File | Changes |
|------|---------|
| `main/thermostat/ui_animation_timing.h` | NEW - all timing constants |
| `main/thermostat/ui_entrance_anim.c` | NEW - entrance animation orchestration |
| `main/thermostat/ui_entrance_anim.h` | NEW - public API |
| `main/thermostat/ui_splash.c` | Coordinate fade with LED, increase duration to 2000ms |
| `main/thermostat/ui_splash.h` | Expose `thermostat_splash_begin_fade()` |
| `main/thermostat/thermostat_led_status.c` | Increase white fade to 1200ms, trigger splash fade |
| `main/thermostat/thermostat_leds.c` | Use timing constants |
| `main/thermostat/ui_top_bar.c` | Add getters for group objects |
| `main/thermostat/ui_actions.c` | Add getters for icon objects, touch blocking check |
| `main/thermostat/ui_setpoint_view.c` | Add getters for tracks/labels |
| `main/thermostat/ui_setpoint_input.c` | Touch blocking check |
| `main/thermostat/ui_helpers.c` | Animate color transitions in `thermostat_setpoint_apply_active_styles()` |
| `main/thermostat_ui.c` | Call entrance anim prepare/start |
