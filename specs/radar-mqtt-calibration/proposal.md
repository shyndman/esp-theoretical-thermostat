## Why

The thermostat already accepts imperative MQTT commands on its Theo-owned command topic, and the vendored LD2410 library already exposes automatic threshold calibration for firmware 2.44+. Adding a radar calibration command lets operators trigger that sensor-native routine without reflashing firmware or using the Bluetooth app, while keeping the contract narrow and operationally legible through logs.

## What Changes

- Add a new MQTT device command payload, `radar_calibrate`, on the existing `<TheoBase>/command` topic.
- Route the new command from the MQTT dataplane into the radar subsystem instead of handling calibration logic in the dataplane.
- Add a radar-owned calibration entrypoint that attempts to start LD2410 automatic threshold calibration with a fixed 10-second clear-room timeout.
- Log the full start narrative for calibration attempts: requested, rejected preconditions, starting with timeout/context, and accepted-by-sensor versus failed-to-start.
- Keep the command fire-and-forget: the device attempts to start calibration and logs launch outcomes, but does not wait for completion, publish status topics, or suppress other runtime behavior during calibration.

## Capabilities

### New Capabilities
- None.

### Modified Capabilities
- `thermostat-connectivity`: extend the existing Theo device command topic so `radar_calibrate` is recognized as a valid command payload and forwarded into the radar subsystem.
- `radar-presence-sensing`: add a radar-owned operation to start LD2410 automatic threshold calibration with a fixed 10-second timeout and explicit launch logging.

## Impact

- Affected code: `main/connectivity/mqtt_dataplane.c`, `main/sensors/radar_presence.{c,h}`.
- Affected APIs: introduces a new internal radar control entrypoint callable from the MQTT dataplane.
- Affected systems: Theo MQTT command handling, LD2410 UART command flow, runtime logging.
- Dependencies: continues using the vendored `cosmavergari/ld2410` component and its firmware-2.44 automatic threshold support; no new external dependencies.
