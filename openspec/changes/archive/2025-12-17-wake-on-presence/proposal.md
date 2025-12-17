# wake-on-presence

## Summary

Add mmWave radar presence detection (LD2410C) to the thermostat, enabling:
1. Backlight wake when a person approaches within 1 meter and remains for 1 second
2. Backlight stay-awake while any presence is detected at any distance
3. MQTT publication of binary presence state and detection distance for Home Assistant occupancy automation

## Motivation

The current backlight behavior relies solely on touch interaction and fixed timeouts. This wastes display lifetime when no one is present and requires physical touch to wake. A presence sensor enables:
- Hands-free wake when approaching the thermostat
- Power savings by dimming only when the room is truly unoccupied
- Occupancy data for smarter HVAC decisions in Home Assistant

## Scope

### In Scope
- LD2410C UART integration (read-only, 256000 baud)
- Periodic data frame parsing (presence state, distances, energies)
- MQTT telemetry: binary presence + detection distance
- Backlight manager integration with debounced wake and presence-hold logic
- Kconfig options for GPIO pins and wake parameters
- Graceful degradation if radar unresponsive (warn, continue operating)

### Out of Scope
- Radar configuration commands (use Bluetooth app for pre-configuration)
- Engineering mode / per-gate energy sensors
- UI display of presence data
- Boot halt on radar failure

## Approach

Use the existing `cosmavergari/ld2410` ESP-IDF component (available locally at `/home/shyndman/dev/github.com/esp32-ld2410` or via `idf.py add-dependency "cosmavergari/ld2410"`). This component provides:
- UART initialization and frame parsing
- Device handle pattern with getters (`ld2410_presence_detected()`, `ld2410_detected_distance()`, etc.)
- Full protocol support including configuration commands (which we won't use, but available if needed later)

Build on this with a thin wrapper (`main/sensors/radar_presence.c/h`):
1. Initialize the ld2410 component, spawn a polling task calling `ld2410_check()`
2. Cache latest readings with mutex protection
3. MQTT publishes via existing `mqtt_manager` when ready
4. `backlight_manager` queries presence state to:
   - Wake on close proximity (< 1m) sustained for 1s
   - Suppress idle timeout while any presence detected
   - Start idle countdown when presence lost

## Spec Deltas

| Capability | Action |
|------------|--------|
| `radar-presence-sensing` | **NEW** - Hardware init, frame parsing, MQTT telemetry, availability lifecycle |
| `thermostat-connectivity` | **MODIFIED** - Add radar topics to telemetry map |
| `backlight_manager` | Integration via new presence query API (no spec change, internal wiring) |

## Related Changes

None - this is a standalone feature addition.
