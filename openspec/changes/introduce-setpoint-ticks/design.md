# Design: Setpoint tick overlay

## Goals
- Provide a permanent visual reference for every 0.5 °C increment between `THERMOSTAT_MIN_TEMP_C` and `THERMOSTAT_MAX_TEMP_C` so users can line up the sliders precisely.
- Keep the overlay subtle and independent of track colors so it stays legible whether cooling (blue) or heating (orange) is active.
- Minimize churn: reuse existing slider geometry math and DO NOT animate or reparent objects whenever the active target switches.

## Overlay container
- A single `lv_obj_t *tick_overlay` lives alongside the setpoint containers but draws on top of the track layer (created after tracks, flagged with `LV_OBJ_FLAG_OVERFLOW_VISIBLE`).
- The overlay is 12 px wide and attaches to the screen edges: left edge (x=0) for cooling, right edge (x=screen_width−12) for heating.
- `lv_obj_set_x` positions the overlay at the appropriate edge. Translation happens immediately with no animation.
- Vertical placement anchors to `THERMOSTAT_TRACK_TOP_Y` minus half the stroke width for padding, and height equals the track span plus full stroke width, ensuring top and bottom ticks render without clipping.

## Tick population
- When the overlay initializes, it builds `((THERMOSTAT_MAX_TEMP_C - THERMOSTAT_MIN_TEMP_C) / 0.5f) + 1` child `lv_line` objects (29 with today's 14–28 °C span) representing every 0.5 °C increment from `THERMOSTAT_MIN_TEMP_C` through `THERMOSTAT_MAX_TEMP_C` inclusive.
- Each line computes its Y coordinate via `thermostat_track_y_from_temperature(temp)` so ticks align perfectly with the slider.
- Whole-degree ticks use a 12 px horizontal line with a 12 px stroke (band-like); half-degree ticks use a 7 px line with an 8 px stroke, maintaining visual hierarchy.
- Ticks are semi-transparent white (~18% opacity) so they remain subtle yet visible over both track colors.

## Lifecycle & invalidation
- The overlay is built once during `thermostat_create_tick_overlay` and never re-parented; only x-position changes when the active target updates.
- If the active target flips, we reposition the overlay and call `lv_obj_invalidate` so LVGL redraws it on the new side.
- Geometry is calculated once at initialization (since `THERMOSTAT_MIN_TEMP_C`/`MAX` are compile-time constants) and reused for the life of the UI, avoiding per-frame draw handlers entirely.
- Because ticks are individual objects, they follow LVGL's z-ordering automatically, ensuring they render above the background track fills but below floating labels.
