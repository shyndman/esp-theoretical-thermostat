# Home Assistant Connectivity

The thermostat UI consumes Home Assistant state exclusively over MQTT. Home Assistant’s `mqtt_statestream` integration publishes entity updates into topics of the form `base_topic/<domain>/<entity>/<attribute>`. Payloads are JSON-serialized scalars (strings quoted, numbers bare). We subscribe to the specific topics below, deserialize into native types, and push changes into LVGL widgets or internal state machines. No server-side responsibilities are documented here.

## Broker Configuration

- **Transport/URI:** `ws://ha-mosquitto-ws.don:80/mqtt` (MQTT over WebSocket, no TLS).
- **Authentication:** none (no username/password).
- **Clean session:** `true` (Statestream republishes retained values; no need to persist subscriptions).
- **Keepalive:** 60 s.
- **Reconnect backoff:** start at 5 s and cap at 30 s to avoid hammering the broker.
- Configure the ESP-MQTT client with `.broker.address.transport = MQTT_TRANSPORT_OVER_WS` and `.broker.address.path = "/mqtt"`.

## MQTT Client Expectations

- Use ESP-IDF’s `esp-mqtt` client for all connectivity: one instance, auto-reconnect enabled. Subscribe to every topic at QoS 0 and publish `temperature_command` at QoS 1 for guaranteed delivery. TLS can be enabled via the client config when brokers require it.
- Subscribe immediately after `MQTT_EVENT_CONNECTED` using explicit topic filters (preferred) or a wildcard (`homeassistant/#`) if dynamic expansion is needed. Handle `MQTT_EVENT_DATA` fragments by concatenating when `total_data_len` exceeds buffer size.
- Leverage the library’s retransmission behavior for QoS>0 publishes (e.g., pushing updated setpoints back to HA). Watch outbox pressure with `esp_mqtt_client_get_outbox_size()` to avoid memory spikes.
- Statestream sometimes republishes retained states; clear cached readiness flags only when fresh data is parsed to avoid UI regressions during reconnects.

## Required Topics

All topics use the `base_topic` `homeassistant`.

| MQTT Topic | Payload | UI Behavior / Notes |
| --- | --- | --- |
| `homeassistant/sensor/pirateweather_temperature/state` | QoS 0, JSON number (°C) | Update weather temperature label. If payload is missing or non-numeric, show `ERR` in red. |
| `homeassistant/sensor/pirateweather_summary/state` | QoS 0, JSON string | Map to LVGL icon set (sunny, clear-night, windy-variant, etc.). Hide the icon if an unknown condition arrives. |
| `homeassistant/sensor/thermostat_target_room_temperature/state` | QoS 0, JSON number (°C) | Display verbatim (no clamping) and update slider heuristics. Invalid payload → `ERR` in red. |
| `homeassistant/climate/theoretical_thermostat_ctrl_climate_control/target_temp_low` | QoS 0, JSON number (°C) | Reposition heating slider. When this update comes in while the backlight is off, wake the backlight (respecting quiet hours), animate the track, then return to sleep 5 s after the animation if the wake originated from this event. |
| `homeassistant/climate/theoretical_thermostat_ctrl_climate_control/target_temp_high` | QoS 0, JSON number (°C) | Same behavior as low setpoint but for the cooling track. |
| `homeassistant/sensor/thermostat_target_room_name/state` | QoS 0, JSON string | Swap room icon; hide the icon if the value is unrecognized. |
| `homeassistant/binary_sensor/theoretical_thermostat_ctrl_computed_fan/state` | QoS 0, JSON string (`"on"`/`"off"`) | Toggle fan animation. Any other payload shows `ERROR` in red in the status label. |
| `homeassistant/binary_sensor/theoretical_thermostat_ctrl_computed_heat/state` | QoS 0, JSON string (`"on"`/`"off"`) | Toggle heating indicator; invalid payload updates status label with `ERROR` in red. |
| `homeassistant/binary_sensor/theoretical_thermostat_ctrl_computed_a_c/state` | QoS 0, JSON string (`"on"`/`"off"`) | Toggle cooling indicator; invalid payload updates status label with `ERROR` in red. |
| `homeassistant/sensor/date_time/state` | QoS 0, ISO 8601 string | Used internally for scheduling animations/day-night cues; never displayed directly. |
| `homeassistant/climate/theoretical_thermostat_ctrl_climate_control/temperature_command` (publish) | QoS 1, JSON object `{ "target_temp_high": <number>, "target_temp_low": <number> }` | Outbound command payload whenever the UI commits new setpoints. Statestream won’t echo this; expect the regular setpoint topics above to reflect the change. |

## Payload Validation

- **JSON extraction:** Statestream wraps all primitives as JSON. Parse each payload with `cJSON` (or equivalent); treat parse failures, `null`, Booleans, NaN/Inf, and strings in numeric fields as invalid.
- **Numeric sensors (weather temperature, room temperature):** Accept any finite floating-point value. Invalid values trigger the `ERR` state (already described in the topic table) and leave the previous numeric cache untouched. No clamping is applied.
- **Setpoint topics (`target_temp_low`, `target_temp_high`):** Require finite floats. Clamp to the slider’s legal range `[10.0 °C, 35.0 °C]` before applying to UI state. Out-of-range payloads are logged and clamped; parsing failures leave the previous value unchanged.
- **Binary sensors:** Accept case-insensitive `"on"`/`"off"`. Any other string (or non-string payload) is treated as invalid and raises the red `ERROR` status, leaving the animation/indicator in its previous state.
- **Room name:** Accept the known strings (Living Room, Bedroom, Office, Hallway). Anything else hides the icon and logs a warning.
- **Weather summary:** Accept the LVGL-supported condition names (sunny, clear-night, partlycloudy, cloudy, fog, rainy, pouring, snowy, snowy-rainy, lightning, lightning-rainy, windy, windy-variant, hail). Unknown values hide the icon.
- **Time:** Parse ISO 8601 via ESP-IDF’s `esp_date_time` helpers (or equivalent). If parsing fails, drop the update and keep the prior timestamp.
- **Command payload (`temperature_command`):** Require an object with both `target_temp_high` and `target_temp_low` as finite floats. Clamp using the same `[10,35]` range and ensure `high >= low + thermostat_temp_step`. Reject publishes (log an error) if validation fails.

## Build-Time Configuration

- Add an ESP-IDF Kconfig entry (e.g., `CONFIG_HA_BASE_TOPIC`) defaulting to `homeassistant`. All subscription filters and publish topics must be constructed from this symbol so deployments can remap the namespace without patching code.
