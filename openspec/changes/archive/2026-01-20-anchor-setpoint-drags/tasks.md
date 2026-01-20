# Implementation Tasks

**Reference:** See `design.md` Implementation Guide section for complete code snippets and integration details.

## 1. Add anchor mode state to view model
- [x] 1.1 Add `anchor_mode_active`, `anchor_temperature`, `anchor_y` fields to `thermostat_view_model_t` in `ui_state.h` after line 48 (setpoint_group_y) (see design.md §ui_state.h)
- [x] 1.2 Initialize new fields in `thermostat_vm_init()` in `thermostat_ui.c` after line 77 (g_view_model.drag_active = false) (see design.md)
- [x] 1.3 Build and verify no compilation errors: `idf.py build`

## 2. Add temperature per pixel constant
- [x] 2.1 Add `k_temperature_per_pixel` constant to `ui_setpoint_view.c` after line 21 (k_slider_intercept) (see design.md §ui_setpoint_view.c)
- [x] 2.2 Add `thermostat_get_temperature_per_pixel()` function declaration to `ui_setpoint_view.h` after line 26 (see design.md)
- [x] 2.3 Add `thermostat_get_temperature_per_pixel()` function implementation to `ui_setpoint_view.c` (see design.md §ui_setpoint_view.c)
- [x] 2.4 Build and verify no compilation errors: `idf.py build`

## 3. Implement proportional drag calculation
- [x] 3.1 Add `thermostat_calculate_anchor_temperature()` function declaration to `ui_setpoint_input.c` after line 15 (see design.md §ui_setpoint_input.c)
- [x] 3.2 Add `thermostat_apply_anchor_mode_drag()` function declaration to `ui_setpoint_input.c` (see design.md §ui_setpoint_input.c)
- [x] 3.3 Add `extern float thermostat_get_temperature_per_pixel(void);` declaration to `ui_setpoint_input.c` (see design.md §ui_setpoint_input.c)
- [x] 3.4 Implement `thermostat_calculate_anchor_temperature()` function in `ui_setpoint_input.c` (see design.md §Step 3)
- [x] 3.5 Implement `thermostat_apply_anchor_mode_drag()` function in `ui_setpoint_input.c` (see design.md §Step 4)
- [x] 3.6 Build and verify no compilation errors: `idf.py build`

## 4. Integrate anchor mode detection
- [x] 4.1 Modify `LV_EVENT_PRESSED` case in `thermostat_handle_touch_event()` to detect anchor mode using `(in_cool || in_heat)` (see design.md §Step 5)
- [x] 4.2 Add anchor mode configuration in PRESSED handler (set anchor_mode_active, anchor_temperature, anchor_y) (see design.md §Step 5)
- [x] 4.3 Skip `thermostat_apply_setpoint_touch()` call when anchor_mode_active is true in PRESSED handler (see design.md §Step 5)
- [x] 4.4 Modify `LV_EVENT_PRESSING` case to call `thermostat_apply_anchor_mode_drag()` when anchor_mode_active (see design.md §Step 6)
- [x] 4.5 Modify `LV_EVENT_RELEASED` and `LV_EVENT_PRESS_LOST` cases to call `thermostat_apply_anchor_mode_drag()` when anchor_mode_active (see design.md §Step 7)
- [x] 4.6 Add anchor mode cleanup (reset all three fields to defaults) in RELEASED/PRESS_LOST handlers (see design.md §Step 7)
- [x] 4.7 Build and verify no compilation errors: `idf.py build`

## 5. Handle target switching and remote updates
- [x] 5.1 Modify `thermostat_select_setpoint_target()` to update anchor_temperature when switching targets in anchor mode (see design.md §Step 8)
- [x] 5.2 Modify `thermostat_apply_remote_temperature()` to deactivate anchor mode when remote update arrives (see design.md §Step 9)
- [x] 5.3 Build and verify no compilation errors: `idf.py build`

## 6. Comprehensive testing
- [x] 6.1 Test label anchor behavior: tap cooling label → no jump, smooth proportional drag (see design.md §Testing)
- [x] 6.2 Test normal track clicking: click track outside labels → existing immediate positioning preserved
- [x] 6.3 Test target switching: tap cooling label, drag, tap heating label → seamless switch with anchor update
- [x] 6.4 Test constraint enforcement: try dragging beyond 28°C max → clamps correctly
- [x] 6.5 Test remote updates: anchor mode active, remote update arrives → anchor mode deactivated
- [x] 6.6 Test precision: small 1-pixel movements → proportional temperature changes maintained
- [x] 6.7 Test performance: rapid dragging → no perceptible lag
