# Change: Introduce setpoint tick overlay

## Why
The thermostat's setpoint sliders currently lack a visual guide for whole and half degrees. Users have no reference for which y-positions correspond to full degrees or 0.5 °C increments, making precise adjustments guesswork.

## What Changes
- Add a neutral tick overlay that shows every 0.5 °C increment between 14 °C and 28 °C, aligning exactly with the slider geometry.
- Attach the overlay to whichever setpoint (cooling or heating) is active and snap it instantly to the opposite side when the active target switches.
- Style whole-degree ticks longer/thicker than half-degree ticks so users can differentiate them at a glance.
- Render ticks in the top layer above the track fill while respecting the existing layout and padding of the setpoint containers.

## Impact
- **Specs:** Adds a new capability detailing how setpoint ticks behave and integrate with the UI layout.
- **Code:** Primarily touches `main/thermostat/ui_setpoint_view.c` (overlay creation, positioning, styles) and any shared theme definitions for colors/opa values.
