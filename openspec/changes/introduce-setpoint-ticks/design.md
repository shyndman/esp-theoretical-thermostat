# Design: Setpoint tick overlay

## Goals
- Provide a permanent visual reference for every 0.5 °C increment between `THERMOSTAT_MIN_TEMP_C` and `THERMOSTAT_MAX_TEMP_C` so users can line up the sliders precisely.
- Keep the overlay visually neutral and independent of track colors so it stays legible whether cooling (blue) or heating (orange) is active.
- Minimize churn: reuse existing slider geometry math and DO NOT animate or reparent objects whenever the active target switches.

## Overlay container
- A single `lv_obj_t *tick_overlay` lives alongside the setpoint containers but draws on top of the track layer (created after tracks, flagged with `LV_OBJ_FLAG_OVERFLOW_VISIBLE`).
- The overlay width equals the setpoint container’s inner padding (currently 12 px via `.pad_left_px/.pad_right_px`), i.e., it consumes exactly the label inset that already separates the track from numeric glyphs. This keeps the tick column locked to an existing layout measurement instead of an ad-hoc guess.
- `lv_obj_set_style_translate_x` snaps the overlay between the cooling and heating sides. Translation happens immediately with no animation.
- Vertical placement anchors to `THERMOSTAT_TRACK_TOP_Y`, and height equals `THERMOSTAT_TRACK_PANEL_HEIGHT`, guaranteeing the overlay covers the full usable track span.

## Tick population
- When the overlay initializes (or when min/max constants ever change), it builds `((THERMOSTAT_MAX_TEMP_C - THERMOSTAT_MIN_TEMP_C) / 0.5f) + 1` child `lv_line` objects (27 with today’s 14–28 °C span) representing every 0.5 °C increment from `THERMOSTAT_MIN_TEMP_C` through `THERMOSTAT_MAX_TEMP_C` inclusive.
- Each line computes its Y coordinate via `thermostat_track_y_from_temperature(temp)` so ticks align perfectly with the slider.
- Whole-degree ticks use a shared "long" style that spans the full padding width (12 px) with a 2 px stroke; half-degree ticks use a "short" style equal to ~60% of that width (7 px) with a 1 px stroke so hierarchy remains obvious without leaving the inset.
- Styles reference neutral palette entries (e.g., `THERMOSTAT_COLOR_NEUTRAL` at ~90% opacity) so we benefit from LVGL's caching and can adjust appearance centrally.

## Lifecycle & invalidation
- The overlay is built once during `thermostat_create_setpoint_group` and never re-parented; only translation changes when the active target updates.
- If the active target flips, we translate the overlay and call `lv_obj_invalidate` so LVGL redraws it on the new side.
- Geometry is calculated once at initialization (since `THERMOSTAT_MIN_TEMP_C`/`MAX` are compile-time constants) and reused for the life of the UI, avoiding per-frame draw handlers entirely.
- Because ticks are individual objects, they follow LVGL's z-ordering automatically, ensuring they render above the background track fills but below floating labels.
