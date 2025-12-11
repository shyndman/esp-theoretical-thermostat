# Tasks: tweak-setpoint-ui

## Implementation

### Inactive color styling
- [x] Add `THERMOSTAT_COLOR_COOL_INACTIVE` constant (cool blue at 50% saturation)
- [x] Add `THERMOSTAT_COLOR_HEAT_INACTIVE` constant (heat orange at 50% saturation)
- [x] Update `THERMOSTAT_OPA_LABEL_INACTIVE_COOL` to 40%
- [x] Update `THERMOSTAT_OPA_LABEL_INACTIVE_HEAT` to 40%
- [x] Update `THERMOSTAT_OPA_TRACK_INACTIVE_COOL` to 40%
- [x] Update `THERMOSTAT_OPA_TRACK_INACTIVE_HEAT` to 40%
- [x] Update `thermostat_update_active_setpoint_styles()` to use inactive colors for inactive labels
- [x] Update `thermostat_setpoint_apply_active_styles()` to use inactive colors for inactive tracks

### Label positioning
- [x] Adjust `THERMOSTAT_LABEL_OFFSET` or label Y computation to raise labels by 1 pixel

### Remove tick overlay
- [x] Remove `g_tick_overlay` static variable from `ui_setpoint_view.c`
- [x] Remove `THERMOSTAT_TICK_COUNT` macro from `ui_setpoint_view.c`
- [x] Remove `g_tick_points` array from `ui_setpoint_view.c`
- [x] Remove `thermostat_is_whole_degree()` function from `ui_setpoint_view.c`
- [x] Remove `thermostat_create_tick_overlay()` function from `ui_setpoint_view.c`
- [x] Remove `thermostat_update_tick_overlay_position()` function from `ui_setpoint_view.c`
- [x] Remove tick function declarations from `ui_setpoint_view.h`
- [x] Remove tick dimension constants from `ui_theme.h`
- [x] Remove tick style extern declarations from `ui_theme.h`
- [x] Remove `g_style_tick_whole` and `g_style_tick_half` globals from `ui_theme.c`
- [x] Remove `THERMOSTAT_TICK_OPA` define from `ui_theme.c`
- [x] Remove tick style initialization from `thermostat_theme_init()` in `ui_theme.c`
- [x] Remove `thermostat_create_tick_overlay()` call from `thermostat_ui.c`
- [x] Remove `thermostat_update_tick_overlay_position()` call from `ui_setpoint_input.c`

### Verification
- [x] Build firmware and verify no compilation errors
- [ ] Test on device: confirm inactive setpoints show desaturated colors at 40% opacity
- [ ] Test on device: confirm labels are raised by 1 pixel
- [ ] Test on device: confirm tick overlay is gone
