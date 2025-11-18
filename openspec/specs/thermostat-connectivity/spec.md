# thermostat-connectivity Specification

## Purpose
TBD - created by archiving change add-mqtt-websocket. Update Purpose after archive.
## Requirements
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

### Requirement: MQTT Client Expectations
The firmware SHALL use a single esp-mqtt client instance (transport WebSocket or TCP per existing bootstrap) with auto-reconnect enabled. The client SHALL subscribe to all thermostat topics at QoS 0, publish `temperature_command` at QoS 1, and enable TLS whenever the broker configuration requires it. The esp-mqtt config MUST set `session.disable_clean_session = false` (clean sessions enabled) so each reconnect starts from a fresh subscription slate. After receiving `MQTT_EVENT_CONNECTED`, the firmware SHALL subscribe immediately using explicit topic filters for every topic listed in this spec. Incoming `MQTT_EVENT_DATA` buffers MUST be concatenated when `total_data_len` exceeds the fragment size. The implementation SHALL rely on esp-mqtt’s QoS>0 retransmission behavior for publishes.

#### Scenario: Client Reconnects Cleanly
- **GIVEN** the esp-mqtt client disconnects and later emits `MQTT_EVENT_CONNECTED`
- **WHEN** the event handler fires
- **THEN** it re-subscribes to every Required Topic before processing other work
- **AND** resumes publishing QoS1 `temperature_command` payloads with retransmit guarantees intact
- **AND** because clean sessions are enabled, no stale broker-side subscriptions linger between reconnects.

### Requirement: Required Topics Matrix
The firmware SHALL subscribe/publish to the following Home Assistant topics (default base topic `homeassistant`, configurable). Each inbound topic MUST drive the UI behavior indicated. Invalid payloads SHALL react exactly as described per-topic (status label error text or widget recolor) while leaving cached state untouched unless clamps are specified. The outbound `temperature_command` publish SHALL occur whenever the UI commits new setpoints.

#### Scenario: Passive Weather Update
- **GIVEN** a payload arrives on `homeassistant/sensor/pirateweather_temperature/state`
- **WHEN** the JSON number parses successfully
- **THEN** the weather temperature label updates to the float value verbatim without waking the backlight.

#### Scenario: Remote Setpoint Update While Idle
- **GIVEN** the backlight is currently off
- **AND** quiet hours permit a wake
- **AND** a payload arrives on `homeassistant/climate/theoretical_thermostat_ctrl_climate_control/target_temp_low` or `/target_temp_high`
- **WHEN** the number parses and differs from the cached setpoint
- **THEN** the device wakes the backlight, repositions/animates the affected slider, and schedules it to turn off 5 seconds after the animation if this flow initiated the wake.

#### Required Topics Summary
1. `homeassistant/sensor/pirateweather_temperature/state` (QoS0 textual float string °C, e.g., `23.5`) → update weather temperature label; invalid payload shows `ERR` in red and logs the raw text.
2. `homeassistant/sensor/pirateweather_summary/state` (QoS0 plain string) → map to LVGL icon set; unknown hides the icon.
3. `homeassistant/sensor/thermostat_target_room_temperature/state` (QoS0 textual float string) → display verbatim, update slider heuristics; invalid shows `ERR` and logs the payload.
4. `homeassistant/climate/theoretical_thermostat_ctrl_climate_control/target_temp_low` (QoS0 textual float string) → reposition heating slider; remote updates wake/animate backlight flow as described; invalid payload logs + `ERR`.
5. `homeassistant/climate/theoretical_thermostat_ctrl_climate_control/target_temp_high` (QoS0 textual float string) → same as low track for cooling slider.
6. `homeassistant/sensor/thermostat_target_room_name/state` (QoS0 string) → swap the displayed room icon: `"Living Room"` → `room_living`, `"Bedroom"` → `room_bedroom`, `"Office"` → `room_office`, `"Hallway"` → `room_hallway`. Unknown strings fall back to `room_default` tinted red.
7. `homeassistant/binary_sensor/theoretical_thermostat_ctrl_computed_fan/state` (QoS0 string `on`/`off`) → toggle the action-bar fan icon animation; invalid payloads recolor the icon solid red while leaving its spin state unchanged.
8. `homeassistant/binary_sensor/theoretical_thermostat_ctrl_computed_heat/state` (QoS0 string `on`/`off`) → toggle the top-bar HVAC status label to `HEATING` with the existing orange color/opacity; invalid payloads set that label text to `ERROR` in red.
9. `homeassistant/binary_sensor/theoretical_thermostat_ctrl_computed_a_c/state` (QoS0 string `on`/`off`) → toggle the top-bar HVAC status label to `COOLING` with the existing blue color/opacity; invalid payloads set that label text to `ERROR` in red.
10. Publish `homeassistant/climate/theoretical_thermostat_ctrl_climate_control/temperature_command` (QoS1 JSON object `{ "target_temp_high": <float>, "target_temp_low": <float> }`) when the UI commits new setpoints; brokers may rebroadcast the resulting setpoints on the low/high textual-float topics, but the device does not wait for echoes before updating its UI.

### Requirement: Payload Validation Rules
All MQTT payloads SHALL be parsed directly from their textual form (no JSON envelopes for inbound topics). Numeric sensors accept any finite float encoded as ASCII (leading/trailing whitespace optional) and never clamp except where explicitly stated; parser implementations MUST reject empty strings, lexical junk, `NaN`, or `Inf`, logging the verbatim payload on failure. Setpoint topics MUST clamp floats into `[10.0, 35.0] °C`; out-of-range but parseable values are logged and clamped, while parse failures keep the prior value. Binary sensor topics accept case-insensitive `"on"`/`"off"`; any other payload recolors the affected fan icon or HVAC status label solid red without toggling its animation/text (and logs the raw string). Room names accept `Living Room`, `Bedroom`, `Office`, `Hallway` and map to `room_living`, `room_bedroom`, `room_office`, `room_hallway`; unknown strings fall back to `room_default` tinted red. Weather summary accepts the LVGL set (`sunny`, `clear-night`, `partlycloudy`, `cloudy`, `fog`, `rainy`, `pouring`, `snowy`, `snowy-rainy`, `lightning`, `lightning-rainy`, `windy`, `windy-variant`, `hail`). Command publishes still require both `target_temp_high` and `target_temp_low` floats encoded inside a JSON object, clamped to `[10,35]` and ensuring `high >= low + thermostat_temp_step`; invalid commands SHALL be rejected with an error log and no publish.

#### Scenario: Invalid Payload Handling
- **GIVEN** the fan topic delivers `{ "state": "maybe" }`
- **WHEN** the parser detects a string other than `on`/`off`
- **THEN** the fan icon recolors solid red while staying in its prior spin state, signaling the error locally.
- **AND GIVEN** `target_temp_high` arrives as `42`
- **WHEN** the numeric clamp runs
- **THEN** the value is clamped to `35.0 °C`, logged, and applied to the cooling slider.

### Requirement: UI Dispatcher and Backlight Hooks
All MQTT updates SHALL be funneled through a single data-plane task that dispatches parsed payloads (topic ID, value, timestamp) to the UI thread via queue or ESP event loop. LVGL mutations MUST occur while holding `esp_lv_adapter_lock()` and SHOULD batch co-related updates to reduce lock churn. Remote setpoint updates that arrive while the backlight is off SHALL invoke the existing remote-change flow: turn on the backlight (respecting quiet hours), animate the affected slider(s), then schedule a one-shot timer to turn the backlight off 5 seconds after animation completes if this flow initiated the wake. Weather/room/HVAC updates SHALL NOT wake the backlight.

**Removal Note:** Legacy demo timers (`thermostat_schedule_top_bar_updates`, `thermostat_weather_timer_cb`, `thermostat_room_timer_cb`, `thermostat_hvac_timer_cb`, and the associated RNG-driven helpers in `thermostat_ui.c`/`ui_top_bar.c`) SHALL be deleted so no random UI mutations remain once MQTT data is wired. All weather/room/HVAC fields MUST be driven exclusively by MQTT payloads covered in this spec.

#### Scenario: Remote Setpoint While Display Sleeps
- **GIVEN** the backlight is off and quiet hours allow wakes
- **WHEN** a new `target_temp_low` or `target_temp_high` payload arrives with a value different from the cached setpoint
- **THEN** the dispatcher wakes the backlight, runs `sync_active_setpoint_visuals`, and queues a timer to turn the backlight off 5 seconds after the animation completes unless another event keeps it awake.

### Requirement: Command Publish Flow
When `commit_setpoints` fires after a drag gesture, the firmware SHALL build `{ "target_temp_high": float, "target_temp_low": float }` using clamped, ordered values and publish it to `…/temperature_command` at QoS 1 with retain=false. The UI MAY optimistically update immediately after publishing; no broker echo or timeout requirement exists.

#### Scenario: Publish Confirmation (Optimistic)
- **GIVEN** the user commits new setpoints and the device publishes the QoS1 command
- **WHEN** the publish API reports success
- **THEN** the UI keeps the newly committed slider positions and logs the outgoing payload for traceability; no rollback occurs waiting for echoes.

### Requirement: Base Topic Configuration
A menuconfig entry `CONFIG_THEO_HA_BASE_TOPIC` (default `homeassistant`) SHALL exist. All subscription filters and publish topics MUST be constructed from this base topic so deployments can remap namespaces without patching code.

#### Scenario: Custom Namespace
- **GIVEN** an installer sets `CONFIG_THEO_HA_BASE_TOPIC="lab/ha"`
- **WHEN** the firmware subscribes/publishes
- **THEN** every topic string uses the new prefix (e.g., `lab/ha/sensor/pirateweather_temperature/state`) while preserving the behaviors defined above.

