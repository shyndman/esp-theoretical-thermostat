# improve-power-experience

## Summary

Add LED bias lighting synchronized with display state, and repurpose the power button to control screen sleep instead of HVAC system power.

## Motivation

The thermostat's LED strip currently only activates for HVAC status (heating/cooling waves) or easter egg effects (rainbow, sparkle). When the screen is on with no HVAC demand, the LEDs remain dark. Bias lighting provides ambient illumination that:
- Reduces eye strain by matching screen brightness to surroundings
- Makes the device feel more polished and intentional
- Uses existing hardware with no additional cost

Additionally, users have no way to manually turn off the display. The power button currently toggles HVAC system power (a rarely used function), but would be more useful as a screen sleep control.

## Scope

### In Scope
- Bias lighting: fade in white LEDs at 50% brightness when screen wakes
- Bias lighting: fade out LEDs when screen sleeps
- Bias lighting: HVAC/MQTT effects override bias lighting; bias resumes when effect ends
- Power button: tap to fade screen off immediately
- Power button: set `presence_ignored` flag for wake-on-presence coordination

### Out of Scope
- Configurable bias lighting color or brightness (hardcoded for now)
- HVAC system power control (removed from power button)
- Remote MQTT control of bias lighting

## Approach

1. **LED status controller** gains a `bias_lighting_active` flag. When screen wakes (via `backlight_manager` callback or polling), if no effect is playing, start a fade-in to white @ 50%. When screen sleeps, fade out.

2. **Effect override logic**: when an HVAC wave or MQTT effect starts, bias lighting is implicitly overridden. When the effect ends (including timed effects like rainbow), `apply_hvac_effect()` checks if screen is still on and no HVAC demand exists, then restores bias lighting.

3. **Power button**: `thermostat_power_icon_event()` calls a new `backlight_manager_request_sleep()` that triggers `enter_idle_state()` with a "manual" reason. This also sets a `presence_ignored` flag that `wake-on-presence` will later consume.

4. **Wake-on-presence coordination**: `backlight_manager` gains a `presence_ignored` bool. Power button sets it true. When radar reports presence lost, it clears. While set, presence detection doesn't trigger wake (touch still works).

## Spec Deltas

| Capability | Action |
|------------|--------|
| `thermostat-led-notifications` | **MODIFIED** - Add bias lighting requirement |
| `thermostat-ui-interactions` | **MODIFIED** - Add power button screen control requirement |

## Related Changes

- `wake-on-presence`: consumes `presence_ignored` flag; this change adds the flag, wake-on-presence checks it