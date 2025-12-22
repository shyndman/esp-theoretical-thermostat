# Tasks: Animate the UI

## Implementation

### 1. Timing Constants Infrastructure
- [x] 1.1 Create `main/thermostat/ui_animation_timing.h`
- [x] 1.2 Add intro section: LED timings (white fade-in 1200ms, hold 1200ms, black fade-out 2000ms)
- [x] 1.3 Add intro section: Splash fade timing (2000ms, synchronized)
- [x] 1.4 Add intro section: Top bar stagger (100ms between groups, 600ms fade duration)
- [x] 1.5 Add intro section: Track growth (1200ms duration, 400ms heating delay)
- [x] 1.6 Add intro section: Label fades (400ms duration, 200ms fractional delay)
- [x] 1.7 Add intro section: Action bar stagger (100ms between icons, 600ms fade duration)
- [x] 1.8 Add intro section: Entrance start offset (400ms before fade-out ends)
- [x] 1.9 Add interaction section: Setpoint color transition (300ms)

### 2. LED Ceremony Timing Updates
- [x] 2.1 Include `ui_animation_timing.h` in `thermostat_led_status.c`
- [x] 2.2 Change `thermostat_leds_solid_with_fade()` call in `start_boot_success_sequence()` from 600ms to timing constant (1200ms)
- [x] 2.3 Update `TIMER_STAGE_BOOT_HOLD` schedule to use timing constant
- [x] 2.4 Update `thermostat_leds_off_with_fade_eased()` call to use timing constant (2000ms)

### 3. Splash Fade Coordination
- [x] 3.1 Add `thermostat_splash_begin_fade()` declaration to `ui_splash.h`
- [x] 3.2 Add state tracking in `ui_splash.c` for "waiting for fade signal"
- [x] 3.3 Modify `splash_start_exit_sequence()` to wait for signal instead of fading immediately
- [x] 3.4 Implement `thermostat_splash_begin_fade()` to trigger the fade when called
- [x] 3.5 Update `SPLASH_EXIT_FADE_DURATION` to use timing constant (2000ms)
- [x] 3.6 Call `thermostat_splash_begin_fade()` from LED status `TIMER_STAGE_BOOT_HOLD` callback

### 4. Entrance Animation Module
- [x] 4.1 Create `main/thermostat/ui_entrance_anim.h` with public API
- [x] 4.2 Create `main/thermostat/ui_entrance_anim.c`
- [x] 4.3 Add `thermostat_entrance_anim_prepare()` to set initial hidden states
- [x] 4.4 Add `thermostat_entrance_anim_start()` to begin choreography
- [x] 4.5 Add `thermostat_entrance_anim_is_active()` getter for touch blocking
- [x] 4.6 Add entrance complete callback mechanism

### 5. Element Accessor Functions
- [x] 5.1 Add `thermostat_get_weather_group()` to `ui_top_bar.h/.c`
- [x] 5.2 Add `thermostat_get_hvac_status_group()` to `ui_top_bar.h/.c`
- [x] 5.3 Add `thermostat_get_room_group()` to `ui_top_bar.h/.c`
- [x] 5.4 Add `thermostat_get_mode_icon()` to `ui_actions.h/.c`
- [x] 5.5 Add `thermostat_get_power_icon()` to `ui_actions.h/.c`
- [x] 5.6 Add `thermostat_get_fan_icon()` to `ui_actions.h/.c`
- [x] 5.7 Add label getters to `ui_setpoint_view.h/.c` (cooling whole/fraction, heating whole/fraction)

### 6. Top Bar Entrance Animation
- [x] 6.1 In `thermostat_entrance_anim_prepare()`: Set `g_weather_group` opacity to 0
- [x] 6.2 In `thermostat_entrance_anim_prepare()`: Set `g_hvac_status_group` opacity to 0
- [x] 6.3 In `thermostat_entrance_anim_prepare()`: Set `g_room_group` opacity to 0
- [x] 6.4 In `thermostat_entrance_anim_start()`: Create weather group fade animation (T=0, 600ms)
- [x] 6.5 Create HVAC status group fade animation (T=100ms, 600ms)
- [x] 6.6 Create room group fade animation (T=200ms, 600ms)

### 7. Setpoint Track Growth Animation
- [x] 7.1 Store initial track heights in view model or entrance anim state
- [x] 7.2 In `thermostat_entrance_anim_prepare()`: Set `g_cooling_track` height to 0
- [x] 7.3 In `thermostat_entrance_anim_prepare()`: Set `g_heating_track` height to 0
- [x] 7.4 Create cooling track height animation (T=0, 1200ms, ease-out)
- [x] 7.5 Create heating track height animation (T=400ms, 1200ms, ease-out)
- [x] 7.6 Use `lv_anim_set_exec_cb()` with custom callback to call `lv_obj_set_height()`

### 8. Setpoint Label Entrance Animation
- [x] 8.1 In `thermostat_entrance_anim_prepare()`: Set all setpoint labels opacity to 0
- [x] 8.2 Create cooling whole number label fade (starts at T=1200ms, 400ms duration)
- [x] 8.3 Create cooling fractional label fade (starts at T=1400ms, 400ms duration)
- [x] 8.4 Create heating whole number label fade (starts at T=1600ms, 400ms duration)
- [x] 8.5 Create heating fractional label fade (starts at T=1800ms, 400ms duration)

### 9. Action Bar Entrance Animation
- [x] 9.1 In `thermostat_entrance_anim_prepare()`: Set `g_mode_icon` opacity to 0
- [x] 9.2 In `thermostat_entrance_anim_prepare()`: Set `g_power_icon` opacity to 0
- [x] 9.3 In `thermostat_entrance_anim_prepare()`: Set `g_fan_icon` opacity to 0
- [x] 9.4 Create mode icon fade (starts at T=2200ms, 600ms duration)
- [x] 9.5 Create power icon fade (starts at T=2300ms, 600ms duration)
- [x] 9.6 Create fan icon fade (starts at T=2400ms, 600ms duration)
- [x] 9.7 On fan icon animation complete: Clear entrance active flag

### 10. Touch Blocking During Entrance
- [x] 10.1 Add entrance active check to `thermostat_setpoint_overlay_event()` in `ui_setpoint_input.c`
- [x] 10.2 Add entrance active check to `thermostat_mode_icon_event()` in `ui_actions.c`
- [x] 10.3 Add entrance active check to `thermostat_power_icon_event()` in `ui_actions.c`
- [x] 10.4 Add entrance active check to `thermostat_fan_icon_event()` in `ui_actions.c`

### 11. UI Initialization Integration
- [x] 11.1 Include `ui_entrance_anim.h` in `thermostat_ui.c`
- [x] 11.2 Call `thermostat_entrance_anim_prepare()` at end of `thermostat_ui_init()`
- [x] 11.3 Trigger `thermostat_entrance_anim_start()` from splash fade callback (400ms before fade ends)

### 12. Setpoint Color Transition
- [x] 12.1 Add `animate` parameter to `thermostat_setpoint_apply_active_styles()` signature
- [x] 12.2 Implement LVGL color animation for label text colors (300ms, use timing constant)
- [x] 12.3 Implement LVGL color animation for track bg colors (300ms)
- [x] 12.4 Implement opacity animation for labels and tracks (300ms)
- [x] 12.5 Update `thermostat_update_active_setpoint_styles()` to pass `animate=true` after entrance complete
- [x] 12.6 Ensure transition triggers from both label tap and mode icon tap paths

### 13. Integration and Testing
- [ ] 13.1 Build and flash to hardware
- [ ] 13.2 Verify LED white fade-in is 1200ms
- [ ] 13.3 Verify LED and splash fade-out are synchronized (both 2000ms)
- [ ] 13.4 Verify entrance animations play in correct order with correct timing
- [ ] 13.5 Verify touch is blocked during entrance, works after
- [ ] 13.6 Verify setpoint color transitions work from label tap
- [ ] 13.7 Verify setpoint color transitions work from mode icon tap
- [ ] 13.8 Test quiet hours behavior (LEDs suppressed, UI animations still run)
- [x] 13.9 Update `docs/manual-test-plan.md` with animation test scenarios

### 14. Splash Anchor Cleanup + Whiteout
- [x] 14.1 Add timing constants for splash line fade and stagger
- [x] 14.2 Lock splash status updates and fade out lines at anchor
- [x] 14.3 Fade splash background to white in sync with LED white fade-in
- [x] 14.4 Move boot chime to LED white peak
