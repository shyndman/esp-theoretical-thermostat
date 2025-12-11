# Change: Tweak setpoint UI visual styling

## Why
The setpoint UI needs visual refinements to improve appearance: inactive state colors are inconsistent, labels are slightly mispositioned, and the tick overlay adds visual clutter without sufficient benefit.

## What Changes
- Inactive setpoint colors: Use desaturated (50% of original saturation) versions of cooling/heating colors at 40% opacity, applied consistently to both labels and tracks
- Label positioning: Raise setpoint labels by 1 pixel
- **BREAKING**: Remove tick overlay entirely from the UI

## Impact
- Affected specs: `thermostat-ui-interactions`
- Affected code:
  - `main/thermostat/ui_state.h` - Add inactive color constants, update opacity defines
  - `main/thermostat/ui_setpoint_view.c` - Update label positioning, remove tick overlay code
  - `main/thermostat/ui_setpoint_view.h` - Remove tick function declarations
  - `main/thermostat/ui_theme.c` - Remove tick styles
  - `main/thermostat/ui_theme.h` - Remove tick constants and style declarations
  - `main/thermostat/ui_setpoint_input.c` - Remove tick position update call
  - `main/thermostat_ui.c` - Remove tick overlay creation call
  - `main/thermostat/ui_helpers.c` - Update active style application
