## Context

Port three scratch animations to the main firmware. Replace the existing HVAC pulse with wave animations that convey physical metaphor (heat rises, cold sinks). Add rainbow as an MQTT-triggered easter egg.

## Reference Implementations

These Arduino sketches are the source of truth for the animation algorithms. Port the logic from these files:

| Effect | Source File | Use Case |
|--------|-------------|----------|
| Rainbow | `scratch/rainbow_loop/rainbow_loop.ino` | MQTT easter egg |
| Rising wave | `scratch/rising_warmth/rising_warmth.ino` | Heating indicator |
| Falling wave | `scratch/falling_cool/falling_cool.ino` | Cooling indicator |

Each scratch file includes a README with effect description and tunable parameters.

## Hardware Layout

39 WS2812 pixels in U-shape:
- Left edge: pixels 0–14 (bottom to top)
- Top bar: pixels 15–22 (left to right)
- Right edge: pixels 23–38 (top to bottom)

## Decisions

### Wave position normalization
Waves traverse a normalized 0.0–1.0 path:
- 0.0 = bottom of either side
- 0.5 = top corners (sides meet top bar)
- 1.0 = center of top bar

This allows a single wave position to affect both sides symmetrically and merge naturally at the top.

### Wave parameters (from `scratch/rising_warmth/rising_warmth.ino`)
- `WAVE_COUNT = 2` - evenly spaced pulses
- `WAVE_WIDTH = 0.45f` - pulse width in normalized space
- `WAVE_SPEED = 0.006f` - position increment per 10ms tick
- `PULSE_BRIGHTNESS = 140` - max brightness boost

### Rainbow parameters (from `scratch/rainbow_loop/rainbow_loop.ino`)
- `HUE_SPEED = 2` - hue increment per frame
- `HUE_DENSITY = 7` - hue change per pixel

### HVAC colors (from scratch files)
- Heating: base `#A03805` (deep orange), rising wave
- Cooling: base `#2065B0` (saturated blue), falling wave

### Rainbow orchestration
Status layer manages the 10s timeout, not the LED layer. This keeps the LED layer as a dumb effect engine and matches the existing boot sequence pattern.

### Command topic namespace
Uses `{theo_base}/command` (Theo namespace), not HA namespace. Requires separate initialization since existing topics use `CONFIG_THEO_HA_BASE_TOPIC`.

## Key Files

| File | Changes |
|------|---------|
| `main/thermostat/thermostat_leds.c` | New effects, ~150 lines |
| `main/thermostat/thermostat_leds.h` | 3 new function declarations |
| `main/thermostat/thermostat_led_status.c` | Wave for HVAC, rainbow timer |
| `main/thermostat/thermostat_led_status.h` | 1 new declaration |
| `main/connectivity/mqtt_dataplane.c` | Command topic subscription and handler |
