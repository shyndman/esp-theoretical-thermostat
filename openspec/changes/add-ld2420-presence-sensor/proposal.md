# Proposal: Add LD2420 millimeter-wave presence radar support

## Why
The thermostat needs local presence detection for three use cases: (1) contributing an occupancy signal to the house-wide sensor network via MQTT, (2) waking the display backlight when someone stands close for a moment, and (3) potentially triggering a future camera stream. The LD2420 is a low-cost 24GHz radar module that detects presence and distance through a simple UART protocol.

## What Changes
- Add an LD2420 driver under `main/sensors/ld2420.c` that initializes UART, parses Energy-mode binary frames, and exposes presence (boolean) and distance (cm) readings.
- Spawn a dedicated FreeRTOS task to read the UART stream, decode frames, and update cached state.
- Publish presence and distance to the Theo-owned MQTT namespace (following the pattern from `add-environmental-telemetry`) with Home Assistant discovery payloads.
- Provide a debounced "close approach" callback API so consumers (backlight_manager, future camera trigger) can react when someone stands within a configurable distance for a configurable hold time.
- Boot is non-blocking: if the sensor is absent or unresponsive, log a warning and continue without halting.
- Introduce Kconfig entries for UART pins (RX=38, TX=37 default), enable flag, close-approach distance threshold, and hold time.

## Impact
- Affected specs: new `presence-sensing` capability
- Affected code: `main/sensors/ld2420.c/.h`, `main/Kconfig.projbuild`, `main/app_main.c` (boot integration), potentially `main/thermostat/backlight_manager.c` (callback registration)
- Depends on: Theo MQTT namespace established by `add-environmental-telemetry` (can be implemented in parallel but MQTT publishing requires that change to land first or be merged together)
