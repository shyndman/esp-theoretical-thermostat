# improve-power-experience Tasks

## Implementation

### Bias Lighting
- [ ] Add `bias_lighting_active` state to `thermostat_led_status.c`
- [ ] Implement `thermostat_led_status_on_screen_wake()` - fade in white @ 50% if no effect active
- [ ] Implement `thermostat_led_status_on_screen_sleep()` - fade out LEDs
- [ ] Modify `apply_hvac_effect()` to restore bias lighting when HVAC stops and screen is on
- [ ] Modify timed effect expiry handler to restore bias lighting instead of fading to off

### Power Button
- [ ] Add `presence_ignored` state to `backlight_manager.c`
- [ ] Implement `backlight_manager_request_sleep()` - sets presence_ignored, calls enter_idle_state
- [ ] Add stub for clearing `presence_ignored` when presence lost (consumed by wake-on-presence)
- [ ] Replace power button handler to call `backlight_manager_request_sleep()` instead of toggling system_powered

### Backlight Manager Integration
- [ ] Call `thermostat_led_status_on_screen_wake()` from `exit_idle_state()`
- [ ] Call `thermostat_led_status_on_screen_sleep()` from `enter_idle_state()`

### Cleanup
- [ ] Remove `g_view_model.system_powered` toggle logic from power button handler
- [ ] Validate no other code depends on `system_powered` state

### Testing
- [ ] Verify bias lighting fades in on touch wake
- [ ] Verify bias lighting fades out on idle timeout
- [ ] Verify HVAC wave overrides bias lighting
- [ ] Verify bias lighting resumes after HVAC stops
- [ ] Verify power button puts display to sleep
- [ ] Verify touch wakes display after power button sleep
