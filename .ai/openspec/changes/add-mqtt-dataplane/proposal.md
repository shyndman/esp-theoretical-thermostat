# Change: Add MQTT Dataplane

## Why
1. The thermostat ships with a WebSocket MQTT bootstrap but lacks a spec for how the data plane behaves once connected, leaving topic coverage, QoS, and UI expectations undefined.
2. Statestream’s retained payloads and Home Assistant requirements already guide the UI today; formalizing them prevents regressions as we keep iterating on the LVGL experience.
3. Without a spec, we risk mismatched topic filters, broken wake flows, and unbounded payload parsing bugs when reconnecting.

## What Changes
1. Define a single esp-mqtt client with auto-reconnect, QoS policies (subscribe QoS0, publish QoS1), TLS hooks, and event-handling expectations.
2. Document every required topic, payload contract, error handling, and UI reaction, including backlight wake rules for remote setpoint changes.
3. Capture payload validation rules (numeric ranges, string enumerations, JSON parsing) plus the command publish contract for `temperature_command`.
4. Specify the dispatcher, LVGL locking discipline, and the Kconfig base-topic symbol so deployments can retarget namespaces.

## Impact
- Specs: `thermostat-connectivity` gains a comprehensive MQTT data-plane requirement set.
- Code: `main/connectivity/` (MQTT manager, dispatcher), `main/thermostat/` (view-model + backlight hooks), and Kconfig defaults will need to follow the new contracts.
- Testing: Requires on-device validation with Statestream/Home Assistant plus payload-fuzz logging to ensure ERR/ERROR states render as required.
