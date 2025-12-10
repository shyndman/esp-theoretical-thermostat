# Tasks: tweak-setpoint-ui

## Implementation

### Inactive color styling
- [ ] Add `THERMOSTAT_COLOR_COOL_INACTIVE` constant (cool blue at 50% saturation)
- [ ] Add `THERMOSTAT_COLOR_HEAT_INACTIVE` constant (heat orange at 50% saturation)
- [ ] Update `THERMOSTAT_OPA_LABEL_INACTIVE_COOL` to 40%
- [ ] Update `THERMOSTAT_OPA_LABEL_INACTIVE_HEAT` to 40%
- [ ] Update `THERMOSTAT_OPA_TRACK_INACTIVE_COOL` to 40%
- [ ] Update `THERMOSTAT_OPA_TRACK_INACTIVE_HEAT` to 40%
- [ ] Update `thermostat_update_active_setpoint_styles()` to use inactive colors for inactive labels
- [ ] Update `thermostat_setpoint_apply_active_styles()` to use inactive colors for inactive tracks

### Label positioning
- [ ] Adjust `THERMOSTAT_LABEL_OFFSET` or label Y computation to raise labels by 1 pixel

### Remove tick overlay
- [ ] Remove `g_tick_overlay` static variable from `ui_setpoint_view.c`
- [ ] Remove `THERMOSTAT_TICK_COUNT` macro from `ui_setpoint_view.c`
- [ ] Remove `g_tick_points` array from `ui_setpoint_view.c`
- [ ] Remove `thermostat_is_whole_degree()` function from `ui_setpoint_view.c`
- [ ] Remove `thermostat_create_tick_overlay()` function from `ui_setpoint_view.c`
- [ ] Remove `thermostat_update_tick_overlay_position()` function from `ui_setpoint_view.c`
- [ ] Remove tick function declarations from `ui_setpoint_view.h`
- [ ] Remove tick dimension constants from `ui_theme.h`
- [ ] Remove tick style extern declarations from `ui_theme.h`
- [ ] Remove `g_style_tick_whole` and `g_style_tick_half` globals from `ui_theme.c`
- [ ] Remove `THERMOSTAT_TICK_OPA` define from `ui_theme.c`
- [ ] Remove tick style initialization from `thermostat_theme_init()` in `ui_theme.c`
- [ ] Remove `thermostat_create_tick_overlay()` call from `thermostat_ui.c`
- [ ] Remove `thermostat_update_tick_overlay_position()` call from `ui_setpoint_input.c`

### Verification
- [ ] Build firmware and verify no compilation errors
- [ ] Test on device: confirm inactive setpoints show desaturated colors at 40% opacity
- [ ] Test on device: confirm labels are raised by 1 pixel
- [ ] Test on device: confirm tick overlay is gone
