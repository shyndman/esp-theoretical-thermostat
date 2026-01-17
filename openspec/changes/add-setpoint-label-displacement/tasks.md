# Implementation Tasks

**Reference:** See `design.md` Implementation Guide section for complete code snippets and integration details.

## 1. Add displacement state and constants
- [ ] 1.1 Add `THERMOSTAT_ANIM_LABEL_DISPLACEMENT_MS` constant (250) to `ui_animation_timing.h` after line 56 (see design.md §2)
- [ ] 1.2 Add `label_displacement_state_t` struct to `ui_setpoint_input.c` after line 9 (see design.md §1)
- [ ] 1.3 Initialize `s_displacement` state in `thermostat_create_setpoint_overlay()` after line 85 (see design.md §1)

## 2. Implement bounds checking helpers
- [ ] 2.1 Add `thermostat_get_container_natural_bounds()` static function before `thermostat_setpoint_overlay_event()` (see design.md §3.1)
- [ ] 2.2 Add `thermostat_point_in_bounds()` static function (see design.md §3.2)

## 3. Implement displacement animation
- [ ] 3.1 Add `translate_x_anim_exec()` callback wrapper for LVGL animation (see design.md §3.3 note)
- [ ] 3.2 Add `thermostat_animate_container_displacement()` function (see design.md §3.3)
  - Calculates target translate_x based on container width and direction
  - Cancels existing animations with `lv_anim_del()`
  - Creates 250ms ease-in-out animation following pattern from `ui_helpers.c:93-108`
- [ ] 3.3 Add `thermostat_update_displacement()` function (see design.md §3.4)
  - Gets active container and natural bounds
  - Checks if touch point intersects bounds
  - Updates state and triggers animation if changed
- [ ] 3.4 Add `thermostat_reset_displacement()` function (see design.md §3.5)

## 4. Integrate with touch handling
- [ ] 4.1 Modify `thermostat_setpoint_overlay_event()` line 119 to pass `point` instead of `point.y` (see design.md §4.1)
- [ ] 4.2 Change `thermostat_handle_touch_event()` signature to accept `lv_point_t screen_point` instead of `lv_coord_t screen_y` (see design.md §4.2)
- [ ] 4.3 Update all `screen_y` references in `thermostat_handle_touch_event()` to `screen_point.y`
- [ ] 4.4 Add `thermostat_update_displacement(screen_point)` call in LV_EVENT_PRESSED case after line 160 (see design.md §4.3)
- [ ] 4.5 Add `thermostat_update_displacement(screen_point)` call in LV_EVENT_PRESSING case (see design.md §4.3)
- [ ] 4.6 Add `thermostat_reset_displacement(g_view_model.active_target)` call in LV_EVENT_RELEASED/PRESS_LOST cases before setting drag_active = false (see design.md §4.3)
- [ ] 4.7 Add `thermostat_reset_displacement(g_view_model.active_target)` call at start of `thermostat_select_setpoint_target()` (see design.md §4.4)

## 5. Build and initial testing
- [ ] 5.1 Build firmware and verify no compilation errors
- [ ] 5.2 Flash to device and check for runtime errors in logs
- [ ] 5.3 Verify basic touch interaction still works (can adjust setpoints)

## 6. Functional validation
- [ ] 6.1 Test cooling label displacement: tap label → slides right ~260px smoothly (see design.md §6)
- [ ] 6.2 Test heating label displacement: tap label → slides left ~260px smoothly
- [ ] 6.3 Test return on finger move: drag off label → animates back to origin, setpoint continues to update
- [ ] 6.4 Test return on release: release while on label → animates back to origin
- [ ] 6.5 Test target switch: displace cooling, switch to heating → cooling returns, heating not displaced
- [ ] 6.6 Test animation timing: verify ~250ms duration, smooth ease-in-out (not linear)
- [ ] 6.7 Test edge cases: rapid tapping, switching targets mid-displacement, releasing during animation
