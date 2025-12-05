# Change: Update MQTT entity IDs

## Why
Home Assistant renamed the thermostat-related entity IDs (pirateweather summary, room targets, climate controller, and computed HVAC states). Our firmware, spec, and manual currently reference the legacy `_ctrl_` versions, so the UI will no longer react once the backend flips.

## What Changes
- Update the thermostat-connectivity spec to list the new entity IDs and scenarios using `sensor.pirateweather_icon`, `sensor.theoretical_thermostat_target_room_*`, `climate.theoretical_thermostat_climate_control`, and `binary_sensor.theoretical_thermostat_computed_*`.
- Refresh `main/connectivity/mqtt_dataplane.c` topic suffixes so subscriptions/publishes align with the renamed entities.
- Adjust supporting docs (manual test plan, logging expectations) to match the new MQTT namespace.

## Impact
- Affected specs: `thermostat-connectivity`
- Affected code: `main/connectivity/mqtt_dataplane.c`, `docs/manual-test-plan.md`
