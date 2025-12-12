# improve-power-experience Design

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────────────────┐
│                         backlight_manager.c                             │
│  ┌───────────────────────────────────────────────────────────────────┐  │
│  │ State:                                                            │  │
│  │  • idle_sleep_active (existing)                                   │  │
│  │  • presence_ignored (NEW - for wake-on-presence coordination)     │  │
│  └───────────────────────────────────────────────────────────────────┘  │
│                                                                         │
│  enter_idle_state() ──────────────┬─────────────────────────────────────│
│                                   ▼                                     │
│                    thermostat_led_status_on_screen_sleep()              │
│                                                                         │
│  exit_idle_state() ───────────────┬─────────────────────────────────────│
│                                   ▼                                     │
│                    thermostat_led_status_on_screen_wake()               │
│                                                                         │
│  NEW: backlight_manager_request_sleep()                                 │
│       → sets presence_ignored = true                                    │
│       → calls enter_idle_state("manual")                                │
└─────────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                      thermostat_led_status.c                            │
│  ┌───────────────────────────────────────────────────────────────────┐  │
│  │ State:                                                            │  │
│  │  • timed_effect_active (existing)                                 │  │
│  │  • heating / cooling (existing)                                   │  │
│  │  • bias_lighting_active (NEW)                                     │  │
│  └───────────────────────────────────────────────────────────────────┘  │
│                                                                         │
│  on_screen_wake():                                                      │
│    IF NOT (heating OR cooling OR timed_effect_active):                  │
│      thermostat_leds_solid(WHITE_50_PERCENT)                            │
│      bias_lighting_active = true                                        │
│                                                                         │
│  on_screen_sleep():                                                     │
│    thermostat_leds_off()                                                │
│    bias_lighting_active = false                                         │
│                                                                         │
│  apply_hvac_effect() (MODIFIED):                                        │
│    IF heating: start heat wave, bias_lighting_active = false            │
│    ELSE IF cooling: start cool wave, bias_lighting_active = false       │
│    ELSE IF screen is on AND NOT timed_effect_active:                    │
│      restore bias lighting                                              │
│                                                                         │
│  on_timed_effect_end() (MODIFIED):                                      │
│    IF screen is on AND NOT (heating OR cooling):                        │
│      restore bias lighting                                              │
└─────────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                         ui_actions.c                                    │
│                                                                         │
│  thermostat_power_icon_event() (MODIFIED):                              │
│    IF screen is idle (already off): return (wake handled by touch)      │
│    ELSE: call backlight_manager_request_sleep()                         │
│                                                                         │
│  (REMOVED: g_view_model.system_powered toggle logic)                    │
└─────────────────────────────────────────────────────────────────────────┘
```

## Bias Lighting Behavior

### Color and Brightness
- Color: Pure white (`0xFFFFFF`)
- Brightness: 50% (0.5f) via new `thermostat_leds_solid_with_fade_brightness()` function
- Fade duration: 100ms

Note: The existing `thermostat_leds_solid_with_fade()` always fades to 100% brightness. A new variant is needed that accepts a target brightness parameter.

### State Transitions

| Event | Current State | Action |
|-------|---------------|--------|
| Screen wakes | No effect | Fade in bias lighting |
| Screen wakes | HVAC wave active | Continue wave (no bias) |
| Screen wakes | Timed effect active | Continue effect (no bias) |
| Screen sleeps | Any | Fade out all LEDs |
| HVAC starts | Bias active | Wave overrides, bias_active = false |
| HVAC stops | Screen on, no timed effect | Restore bias |
| Timed effect starts | Bias active | Effect overrides, bias_active = false |
| Timed effect ends | Screen on, no HVAC | Restore bias |

## Power Button Behavior

### Current Behavior (to be removed)
```c
g_view_model.system_powered = !g_view_model.system_powered;
if (!g_view_model.system_powered) {
  g_view_model.fan_running = false;
}
```

### New Behavior
```c
// If we're waking from idle, the touch was consumed; don't also sleep
if (backlight_manager_notify_interaction(BACKLIGHT_WAKE_REASON_TOUCH)) {
  return;
}
// Screen is on, user wants to sleep
backlight_manager_request_sleep();
```

### Wake-on-Presence Coordination

The `presence_ignored` flag enables manual sleep to persist even when presence is detected:

1. User presses power → `presence_ignored = true`, screen sleeps
2. User is still standing there → radar sees presence, but wake suppressed
3. User walks away → radar reports presence lost → `presence_ignored = false`
4. User returns → normal presence wake resumes

This logic lives in `backlight_manager.c` but is consumed by `wake-on-presence` change. The flag is:
- Set by `backlight_manager_request_sleep()`
- Cleared when `radar_presence_get_state()` reports no presence (polled by presence timer)
- Checked in presence wake logic before triggering `exit_idle_state()`

## Files Modified

| File | Changes |
|------|---------|
| `main/thermostat/thermostat_leds.c` | Add `thermostat_leds_solid_with_fade_brightness()` function |
| `main/thermostat/thermostat_leds.h` | Declare `thermostat_leds_solid_with_fade_brightness()` |
| `main/thermostat/thermostat_led_status.c` | Add bias lighting state, screen wake/sleep handlers |
| `main/thermostat/thermostat_led_status.h` | Declare `on_screen_wake()`, `on_screen_sleep()` |
| `main/thermostat/backlight_manager.c` | Add `presence_ignored`, `request_sleep()`, call LED status on state change |
| `main/thermostat/backlight_manager.h` | Declare `backlight_manager_request_sleep()` |
| `main/thermostat/ui_actions.c` | Replace power button system_powered toggle with request_sleep() |

## Kconfig Options

None required. Bias lighting brightness/color could be made configurable later if needed.
