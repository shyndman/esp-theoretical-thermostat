## ADDED Requirements

### Requirement: Device Availability via MQTT Last Will
The firmware SHALL expose a retained device-level availability topic at `<TheoBase>/<Slug>/availability` using payloads `online` / `offline`, where:
- `<TheoBase>` is the normalized Theo publish base derived from `CONFIG_THEO_THEOSTAT_BASE_TOPIC` (default `theostat`).
- `<Slug>` is the normalized `CONFIG_THEO_DEVICE_SLUG` (default `hallway`).

The MQTT client MUST configure a Last Will and Testament (LWT) message so that if the device disconnects unexpectedly (power loss or Wi-Fi loss), the broker publishes retained `offline` to `<TheoBase>/<Slug>/availability`.

On each successful MQTT connection (`MQTT_EVENT_CONNECTED`), the firmware MUST publish retained `online` to `<TheoBase>/<Slug>/availability`.

Implementation details (verified against ESP-IDF v5.5.2 / esp-mqtt 1.0.0):
- LWT MUST be configured before calling `esp_mqtt_client_init()`.
- Use `esp_mqtt_client_config_t::session.last_will`:
  - `topic` = `<TheoBase>/<Slug>/availability`
  - `msg` = `"offline"`
  - `msg_len` = `0` when `msg` is NULL-terminated
  - `qos` = `0`
  - `retain` = `1`
- The `online` publish on connect MUST be:
  - topic = `<TheoBase>/<Slug>/availability`
  - payload = `online`
  - qos = 0
  - retain = 1

#### Scenario: Power loss triggers broker-published offline
- **GIVEN** the device has connected to the broker and published retained `online` to `<TheoBase>/<Slug>/availability`
- **WHEN** the device loses power and the MQTT connection drops without a clean disconnect
- **THEN** the broker publishes retained `offline` to `<TheoBase>/<Slug>/availability`.

#### Scenario: Wi-Fi loss triggers broker-published offline
- **GIVEN** the device has connected to the broker and published retained `online` to `<TheoBase>/<Slug>/availability`
- **WHEN** the device loses Wi-Fi connectivity and the MQTT connection drops
- **THEN** the broker publishes retained `offline` to `<TheoBase>/<Slug>/availability`.

#### Scenario: Reconnect publishes online
- **GIVEN** the broker has retained `offline` for `<TheoBase>/<Slug>/availability`
- **WHEN** the device reconnects to MQTT
- **THEN** the firmware publishes retained `online` to `<TheoBase>/<Slug>/availability`.
