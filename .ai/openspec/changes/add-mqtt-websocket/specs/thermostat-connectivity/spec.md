# Capability: Thermostat Connectivity

## ADDED Requirements

### Requirement: MQTT Client Bootstraps over WebSocket
The firmware SHALL create and start a single esp-mqtt client immediately after Wi-Fi becomes ready, using a URI constructed from configuration (`ws://<host>:<port>/<path>`). An `MQTT_EVENT_CONNECTED` log entry SHALL be emitted once the client connects.

#### Scenario: Successful Connection
- **GIVEN** `wifi_remote_manager_start()` returns `ESP_OK`
- **AND** `CONFIG_THEO_MQTT_HOST`/`PORT` are non-empty and resolvable
- **WHEN** the firmware initializes MQTT
- **THEN** it builds a `ws://` URI using host, port, and optional path (default `/mqtt`)
- **AND** starts the client, logging `MQTT_EVENT_CONNECTED` upon success
- **AND** leaves the UI loop running only after the MQTT client has started.

### Requirement: MQTT Startup Failure Halts Boot
If esp-mqtt initialization or start fails, the firmware SHALL log an error and abort further startup (before entering the UI loop) so developers notice broker issues early.

#### Scenario: Client Creation Fails
- **GIVEN** Wi-Fi has connected but the MQTT host is unreachable or config is invalid
- **WHEN** `mqtt_manager_start()` returns an error
- **THEN** `app_main` logs the failure with the URI it attempted
- **AND** skips UI initialization, instead idling or returning so the watchdog resets during bring-up.

### Requirement: MQTT Configuration via Kconfig
The MQTT host, port, and optional path SHALL be defined via `menuconfig` entries (`CONFIG_THEO_MQTT_HOST`, `CONFIG_THEO_MQTT_PORT`, `CONFIG_THEO_MQTT_PATH`). Defaults SHOULD target a local dev broker, and validation MUST reject empty host strings at runtime.

#### Scenario: Invalid Configuration
- **GIVEN** the application is built without setting `CONFIG_THEO_MQTT_HOST`
- **WHEN** `mqtt_manager_start()` runs
- **THEN** it detects the missing host, logs a descriptive error, returns `ESP_ERR_INVALID_STATE`, and prevents MQTT from starting.

### Requirement: MQTT Event Logging for Diagnosis
The firmware SHALL register an MQTT event handler that logs CONNECTED, DISCONNECTED, ERROR, and RECONNECTED transitions with transport (`ws`) and broker URI to aid manual testing.

#### Scenario: Broker Bounce
- **GIVEN** the device is connected to the broker
- **WHEN** the broker restarts
- **THEN** the event handler logs DISCONNECTED and subsequent CONNECTED events
- **AND** the client auto-reconnects using default esp-mqtt behavior without requiring a device reboot.
