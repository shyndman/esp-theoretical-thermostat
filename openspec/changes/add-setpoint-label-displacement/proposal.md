# Change: Add Setpoint Label Displacement

## Why
When users tap directly on setpoint labels to initiate a drag, their finger occludes the temperature value they're adjusting. This makes it difficult to see the setpoint value change during the drag interaction, creating a poor user experience.

## What Changes
- Add horizontal displacement animation for active setpoint labels when touch point intersects label bounds
- Labels slide one container-width toward screen center (cooling slides right, heating slides left)
- Displacement tracks touch position continuously during drag (not just at start)
- Labels return to original position when touch moves off label or on release
- Animation uses 200-300ms ease-in-out timing for smooth motion

## Impact
- Affected specs: thermostat-ui-interactions
- Affected code:
  - `main/thermostat/ui_setpoint_input.c` (touch handling, displacement logic)
  - `main/thermostat/ui_setpoint_view.h` (state tracking)
  - `main/thermostat/ui_animation_timing.h` (timing constants)
