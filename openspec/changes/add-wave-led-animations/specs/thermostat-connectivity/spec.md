## ADDED Requirements

### Requirement: Device command topic
The MQTT dataplane SHALL subscribe to a device-scoped command topic at `<TheoBase>/command` where `<TheoBase>` is the normalized `CONFIG_THEO_THEOSTAT_BASE_TOPIC`. This topic provides a single entry point for device commands with the payload determining the action. Unknown command payloads SHALL be logged at WARN level and ignored. The subscription SHALL occur alongside other topic subscriptions on `MQTT_EVENT_CONNECTED`.

#### Scenario: Rainbow command received
- **GIVEN** the device is connected to MQTT
- **WHEN** a message with payload `rainbow` arrives on `<TheoBase>/command`
- **THEN** the dataplane calls `thermostat_led_status_trigger_rainbow()` to start the timed rainbow effect.

#### Scenario: Unknown command ignored
- **GIVEN** the device is connected to MQTT
- **WHEN** a message with payload `unknown_action` arrives on `<TheoBase>/command`
- **THEN** the dataplane logs a WARN with the unrecognized payload and takes no further action.
