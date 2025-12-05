# Tasks
- [x] Define tick overlay styles (whole vs half) in `ui_theme.c`/`ui_theme.h`: semi-transparent white at 18% opacity, 12 px stroke for whole-degree ticks, 8 px stroke for half-degree ticks.
- [x] Extend `ui_setpoint_view` with a dedicated tick overlay object attached to screen edges (left for cooling, right for heating), with padding to prevent stroke clipping.
- [x] Populate the overlay with `((THERMOSTAT_MAX_TEMP_C - THERMOSTAT_MIN_TEMP_C) / 0.5f) + 1` line children (29 with the current 14–28 °C span) in 0.5 °C steps, with whole-degree ticks rendered as 12 px bands and half-degree as 7 px lines.
- [x] Ensure the overlay always renders above the track backgrounds, snaps instantly when heating/cooling focus changes via `thermostat_update_tick_overlay_position()`.
- [x] Validate on hardware that the ticks align with slider positions and remain legible over both color schemes.
