# Change: Cohesive Boot Transition and UI Entrance Animations

## Why
The current splash-to-main-UI transition feels disjointed: the LED ceremony (sparkle drain → white → hold → black fade) and the splash screen fade run on independent timelines with no coordination. Additionally, the main UI elements simply "pop in" rather than animating into view, and setpoint mode switching is instant rather than smooth.

## What Changes
- **Synchronized LED + Splash fade-out**: LED white→black fade and splash screen fade happen simultaneously over 2000ms
- **Extended LED white fade-in**: Increase from 600ms to 1200ms, splash remains visible throughout
- **Anchor cleanup + whiteout**: Status lines fade out on exit request, screen fades to white in sync with LED white fade-in, and boot chime plays at the white peak
- **Main UI entrance choreography**: Staggered fade-ins and growth animations for all UI elements, starting 400ms before fade-out completes
- **Touch blocking during intro**: UI is non-interactive until entrance animations complete
- **Setpoint color transition**: 300ms color tween when switching active setpoint (heating ↔ cooling)
- **Centralized timing constants**: All animation timings in a dedicated header file for easy tuning

## Impact
- Affected specs: `thermostat-boot-experience`, `thermostat-led-notifications`, `thermostat-ui-interactions`
- Affected code:
  - `main/thermostat/ui_splash.c` - splash fade coordination
  - `main/thermostat/thermostat_led_status.c` - LED ceremony timing
  - `main/thermostat/thermostat_ui.c` - main UI entrance animations
  - `main/thermostat/ui_top_bar.c` - top bar element fade-ins
  - `main/thermostat/ui_actions.c` - action bar element fade-ins
  - `main/thermostat/ui_setpoint_view.c` - track growth, label fade-ins, color transitions
  - New file: `main/thermostat/ui_animation_timing.h` - centralized timing constants
