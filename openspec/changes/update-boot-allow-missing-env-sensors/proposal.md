# Change: Allow Boot Without Environmental Sensors

## Why
Right now, if the AHT20/BMP280 environmental sensors fail to initialize, the device halts boot and reboots.
This creates reboot loops for a non-essential subsystem and prevents the thermostat UI from coming up.

## What Changes
- Environmental sensor hardware initialization becomes non-fatal: the boot sequence continues even if AHT20/BMP280 init fails.
- The splash screen continues to show an error line in red identifying the environmental sensor init failure.
- Home Assistant behavior becomes explicit on missing sensors: the firmware publishes retained discovery config (as today) and retained per-entity `availability=offline` so entities show as unavailable (even if prior retained state exists).
- Environmental sensing remains all-or-nothing: if either AHT20 or BMP280 fails initialization, the env sampling task does not start and no env telemetry is published.
- No auto-retry for boot-time missing hardware: if env init fails during boot, the system remains in the offline state until the next reboot.

## Impact
- Affected specs:
  - `openspec/specs/environmental-sensing/spec.md`
  - `openspec/specs/thermostat-boot-experience/spec.md`
- Affected code (expected):
  - `main/app_main.c` (boot stage handling)
  - `main/sensors/env_sensors.c` / `main/sensors/env_sensors.h` (init semantics + HA availability signaling)
  - MQTT/HA publishing paths that depend on device identity strings (topic base + slug)
