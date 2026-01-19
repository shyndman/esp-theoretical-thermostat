## MODIFIED Requirements

### Requirement: Home Assistant Discovery
The firmware SHALL publish retained Home Assistant MQTT discovery configurations for the radar presence binary sensor and radar distance sensor once MQTT reports ready.

Discovery topics follow the pattern `homeassistant/<component>/<Slug>-theostat/<object_id>/config`.

Discovery payloads SHALL include:
- Device class (`occupancy` for presence, `distance` for distance sensor).
- Unit of measurement (`cm`) for the distance sensor.
- Device information linking to the thermostat device.
- Home Assistant availability configuration using the multi-availability format (verified against Home Assistant MQTT Binary Sensor + MQTT Discovery docs):
  - The payload MUST include `availability_mode="all"`.
  - The payload MUST include an `availability` array with exactly two entries.
  - Each availability entry MUST be an object with keys:
    - `topic`
    - `payload_available`
    - `payload_not_available`
  - Entry 1) Device availability (LWT-backed):
    - `topic` = `<TheoBase>/<Slug>/availability`
    - `payload_available` = `online`
    - `payload_not_available` = `offline`
  - Entry 2) Per-entity radar availability (existing topic for this entity):
    - `topic` = the entity's existing radar availability topic
    - `payload_available` = `online`
    - `payload_not_available` = `offline`

#### Scenario: HA restart discovers radar sensors
- **GIVEN** the thermostat has published discovery configs with retain=true
- **WHEN** Home Assistant restarts and reconnects to the MQTT broker
- **THEN** HA automatically discovers the radar presence and distance sensors
- **AND** displays them under the thermostat device.

#### Scenario: Device offline marks radar entities unavailable
- **GIVEN** Home Assistant has discovered a radar entity with `availability_mode="all"` and device availability topic `<TheoBase>/<Slug>/availability`
- **WHEN** the broker publishes `offline` to `<TheoBase>/<Slug>/availability`
- **THEN** Home Assistant marks the radar entity unavailable even if the last retained state value exists.

#### Scenario: Radar subsystem offline marks entity unavailable
- **GIVEN** Home Assistant has discovered a radar entity with `availability_mode="all"`
- **WHEN** the firmware publishes retained `offline` to the entity's per-radar availability topic
- **THEN** Home Assistant marks the entity unavailable even if the device availability topic remains `online`.
