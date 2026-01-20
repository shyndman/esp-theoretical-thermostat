## ADDED Requirements

### Requirement: Home Assistant Discovery Config
On boot, the firmware SHALL publish retained discovery payloads for the four environmental sensors to `homeassistant/sensor/<Slug>/<object_id>/config`.

Each payload MUST include:
1. `name` = object ID (e.g., `temperature_bmp`).
2. `unique_id` per the telemetry topic map.
3. `device_class`, `state_class="measurement"`, and `unit_of_measurement` matching the sensor.
4. `device` block with `name="<FriendlyName> Theostat"`, `identifiers=["theostat_<Slug>"]`, `manufacturer="YourOrg"`, `model="Theostat v1"`.
5. `state_topic` pointing to the Theo namespace topics.
6. Home Assistant availability configuration using the multi-availability format (verified against Home Assistant MQTT Sensor + MQTT Discovery docs):
   - The payload MUST include `availability_mode="all"`.
   - The payload MUST include an `availability` array with exactly two entries.
   - Each availability entry MUST be an object with keys:
     - `topic`
     - `payload_available`
     - `payload_not_available`
   - Entry a) Device availability (LWT-backed):
     - `topic` = `<TheoBase>/<Slug>/availability`
     - `payload_available` = `online`
     - `payload_not_available` = `offline`
   - Entry b) Per-entity availability (existing per-sensor topic):
     - `topic` = `<TheoBase>/sensor/<Slug>/<object_id>/availability`
     - `payload_available` = `online`
     - `payload_not_available` = `offline`
7. Retain flag enabled so HA rediscovers entities after restarts without waiting for the firmware to re-send configs.

#### Scenario: HA restart sees retained configs
- **GIVEN** the thermostat has already published all four retained discovery configs and availability messages
- **WHEN** Home Assistant restarts
- **THEN** it automatically recreates the device and entities using the retained configs without requiring manual intervention.

#### Scenario: Device offline marks entity unavailable
- **GIVEN** Home Assistant has discovered a sensor entity with `availability_mode="all"` and device availability topic `<TheoBase>/<Slug>/availability`
- **WHEN** the broker publishes `offline` to `<TheoBase>/<Slug>/availability`
- **THEN** Home Assistant marks the entity unavailable even if the last retained state value exists.

#### Scenario: Sensor subsystem offline marks entity unavailable
- **GIVEN** Home Assistant has discovered an environmental sensor entity with `availability_mode="all"`
- **WHEN** the firmware publishes retained `offline` to `<TheoBase>/sensor/<Slug>/<object_id>/availability`
- **THEN** Home Assistant marks the entity unavailable even if the device availability topic remains `online`.
