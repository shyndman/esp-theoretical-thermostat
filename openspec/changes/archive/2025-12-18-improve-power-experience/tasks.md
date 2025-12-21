# improve-power-experience Tasks

## Implementation

### Bias Lighting
- [x] Add `thermostat_leds_solid_with_fade_brightness()` to `thermostat_leds.c/h` - fade to color at target brightness
- [x] Add `bias_lighting_active` state to `thermostat_led_status.c`
- [x] Implement `thermostat_led_status_on_screen_wake()` - fade in white @ 50% if no effect active
- [x] Implement `thermostat_led_status_on_screen_sleep()` - fade out LEDs
- [x] Modify `apply_hvac_effect()` to restore bias lighting when HVAC stops and screen is on
- [x] Modify timed effect expiry handler to restore bias lighting instead of fading to off

### Power Button
- [x] Add `presence_ignored` state to `backlight_manager.c`
- [x] Implement `backlight_manager_request_sleep()` - sets presence_ignored, calls enter_idle_state
- [x] Add stub for clearing `presence_ignored` when presence lost (consumed by wake-on-presence)
- [x] Replace power button handler to call `backlight_manager_request_sleep()` instead of toggling system_powered

### Backlight Manager Integration
- [x] Call `thermostat_led_status_on_screen_wake()` from `exit_idle_state()`
- [x] Call `thermostat_led_status_on_screen_sleep()` from `enter_idle_state()`

### Cleanup
- [x] Remove `g_view_model.system_powered` toggle logic from power button handler
- [x] Validate no other code depends on `system_powered` state

### Testing
- [ ] Verify bias lighting fades in on touch wake
- [ ] Verify bias lighting fades out on idle timeout
- [ ] Verify HVAC wave overrides bias lighting
- [ ] Verify bias lighting resumes after HVAC stops
- [ ] Verify power button puts display to sleep
- [ ] Verify touch wakes display after power button sleep
