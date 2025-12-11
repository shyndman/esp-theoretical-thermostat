# Change: Add wave LED animations for HVAC and rainbow easter egg

## Why
The existing pulse animation for heating/cooling is functional but lacks visual metaphor. Wave animations better convey the physical nature of temperature: heat rises, cold sinks. Additionally, a rainbow easter egg provides a fun demo capability triggered via MQTT.

## What Changes
- **MODIFIED** LED effect primitives: Add wave effect (rising/falling pulses along U-shape) and rainbow effect (cycling hue)
- **MODIFIED** Heating/cooling cues: Replace pulse with wave animations (rising for heat, falling for cool)
- **ADDED** Rainbow trigger: 10-second timed rainbow effect, triggered via MQTT command topic
- **ADDED** MQTT command topic: `{theo_base}/command` for device-scoped commands

## Impact
- Affected specs: `thermostat-led-notifications`, `thermostat-connectivity`
- Affected code:
  - `main/thermostat/thermostat_leds.c` - new effect types and rendering
  - `main/thermostat/thermostat_leds.h` - new public API functions
  - `main/thermostat/thermostat_led_status.c` - wave for HVAC, rainbow timer
  - `main/thermostat/thermostat_led_status.h` - rainbow trigger function
  - `main/connectivity/mqtt_dataplane.c` - command topic subscription and handling
