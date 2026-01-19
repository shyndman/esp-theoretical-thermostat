# Change: Report device availability via MQTT LWT

## Why
The firmware publishes retained sensor state values. Without a device-level availability signal, Home Assistant continues to display the last retained values even when the device disappears (power loss / Wi-Fi loss). This can cause incorrect, long-lived states (e.g., radar presence staying `ON` for hours while the device is offline).

## What Changes
- Add a device-level availability topic (`<TheoBase>/<Slug>/availability`) backed by MQTT Last Will and Testament (LWT).
  - Broker publishes retained `offline` on unexpected disconnect.
  - Firmware publishes retained `online` on every successful connect.
- Update Home Assistant discovery payloads for Theo-published sensors (environmental + radar) to use HA's multi-availability format:
  - `availability_mode="all"`
  - `availability` array includes:
    1) device availability topic (LWT-backed)
    2) existing per-entity availability topic

## Impact
- Affected specs:
  - `thermostat-connectivity` (new device availability requirement)
  - `environmental-sensing` (discovery payload availability format)
  - `radar-presence-sensing` (discovery payload availability format)
- Affected code (expected):
  - `main/connectivity/mqtt_manager.c` (LWT config + publish on connect)
  - `main/sensors/env_sensors.c` (discovery payload JSON format)
  - `main/sensors/radar_presence.c` (discovery payload JSON format)

## Acceptance Criteria
- When the device loses power, the broker publishes retained `offline` to `<TheoBase>/<Slug>/availability` and Home Assistant marks Theo-published entities `unavailable`.
- When Wi-Fi drops (device stays powered), the broker publishes retained `offline` to `<TheoBase>/<Slug>/availability` and Home Assistant marks Theo-published entities `unavailable`.
- When the device reconnects, it publishes retained `online` to `<TheoBase>/<Slug>/availability` and entities recover.
- When a specific sensor subsystem is unhealthy (existing per-entity offline behavior), only that entity becomes unavailable even while the device remains `online`.
