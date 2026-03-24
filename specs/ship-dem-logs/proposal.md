## Why

The thermostat currently relies on local serial logs, which makes iterative development awkward once the device is mounted on the wall. We need a lightweight way to mirror runtime logs over the existing MQTT connection so developers can observe behavior remotely without changing the app’s normal control flow or maintaining a second broker connection.

## What Changes

- Add an `mqtt-log-mirror` capability that mirrors each formatted `ESP_LOGx` line to a single per-device MQTT topic: `<theo_base>/<device_slug>/logs`.
- Keep UART logging as the primary sink. Remote logging is best-effort only: it must never block normal logging, never recurse through `ESP_LOGx` on failure, and does not need persistence or replay.
- Add a `device-identity` capability that becomes the single owner of three shared values:
  - normalized device slug
  - device friendly name
  - Theo base topic
- Move current identity consumers to the new shared identity source so topic naming and discovery metadata are derived in one place.
- Reuse the existing shared `espressif/mqtt` client and its outbox instead of introducing a second MQTT client, a syslog library, or an app-owned log queue.

## Capabilities

### New Capabilities
- `mqtt-log-mirror`: Mirrors already-formatted log lines to MQTT for development-time remote visibility while preserving normal UART logging behavior.
- `device-identity`: Owns slug, friendly name, and Theo base topic early in startup so MQTT-facing modules and diagnostics can share one source of truth.

### Modified Capabilities
- `mqtt-manager`: Continues to own the shared MQTT client, but stops privately deriving its own device identity values.
- `env-sensors`: Stops owning shared identity values and becomes a consumer of the new identity source.
- `device-diagnostics-publishers`: Continue publishing diagnostics and discovery data, but read shared identity from the new module instead of `env_sensors`.

## Impact

- Affected code:
  - startup sequencing in `main/app_main.c`
  - MQTT ownership/helpers in `main/connectivity/mqtt_manager.{c,h}`
  - identity access currently coupled to `main/sensors/env_sensors.{c,h}`
  - diagnostics/discovery publishers such as `device_info`, `device_ip_publisher`, `device_telemetry`, and `radar_presence`
  - new `mqtt_log_mirror` module and new shared `device_identity` module
- APIs:
  - add identity initialization/accessor APIs
  - add log-mirror startup API
  - keep existing `ESP_LOGx` call sites unchanged
- Systems:
  - MQTT observability for wall-mounted development
  - startup ordering for identity-dependent modules
  - topic naming consistency across MQTT publishers
- Dependencies:
  - reuse the existing `espressif/mqtt` dependency and esp-mqtt outbox; the project currently resolves `espressif/mqtt` to `1.0.0` in `dependencies.lock`
  - rely on the workspace ESP-IDF toolchain already in use (`idf.py --version` reports `ESP-IDF v5.5.2-249-gf56bea3d1f-dirty`) for `esp_log_set_vprintf()` / `vprintf_like_t`
  - no new remote logging transport dependency
