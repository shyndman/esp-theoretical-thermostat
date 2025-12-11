## ADDED Requirements

### Requirement: Device command topic
The MQTT dataplane SHALL subscribe to a device-scoped command topic at `<TheoBase>/command` where `<TheoBase>` is the normalized `CONFIG_THEO_THEOSTAT_BASE_TOPIC`. This topic provides a single entry point for device commands with the payload determining the action. Unknown command payloads SHALL be logged at WARN level and ignored. The subscription SHALL occur alongside other topic subscriptions on `MQTT_EVENT_CONNECTED`.

#### Scenario: Rainbow command received
- **GIVEN** the device is connected to MQTT
- **WHEN** a message with payload `rainbow` arrives on `<TheoBase>/command`
- **THEN** the dataplane calls `thermostat_led_status_trigger_rainbow()` to start the timed rainbow effect.

#### Scenario: Heatwave command received
- **GIVEN** the device is connected to MQTT
- **WHEN** a message with payload `heatwave` arrives on `<TheoBase>/command`
- **THEN** the dataplane calls `thermostat_led_status_trigger_heatwave()` to start the timed rising wave effect.

#### Scenario: Coolwave command received
- **GIVEN** the device is connected to MQTT
- **WHEN** a message with payload `coolwave` arrives on `<TheoBase>/command`
- **THEN** the dataplane calls `thermostat_led_status_trigger_coolwave()` to start the timed falling wave effect.

#### Scenario: Sparkle command received
- **GIVEN** the device is connected to MQTT
- **WHEN** a message with payload `sparkle` arrives on `<TheoBase>/command`
- **THEN** the dataplane calls `thermostat_led_status_trigger_sparkle()` to start the timed sparkle effect.

#### Scenario: Unknown command ignored
- **GIVEN** the device is connected to MQTT
- **WHEN** a message with payload `unknown_action` arrives on `<TheoBase>/command`
- **THEN** the dataplane logs a WARN with the unrecognized payload and takes no further action.
