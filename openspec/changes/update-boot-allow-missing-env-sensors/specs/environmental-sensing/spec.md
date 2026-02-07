## MODIFIED Requirements

### Requirement: Environmental Sensor Hardware Initialization
The firmware SHALL initialize an ESP-IDF I2C master bus dedicated to the onboard environmental sensors during boot, using FireBeetle 2 GPIO7 for SDA and GPIO8 for SCL by default (both at 3.3 V with existing external pull-ups). The build MUST add `jack-ingithub/aht20^0.1.1` and `k0i05/esp_bmp280^1.2.7` as managed component dependencies and create one handle per sensor over the shared bus. Sensor bring-up SHALL occur before the splash screen dismisses. If either sensor fails initialization, the firmware MUST report the failure on the splash screen as an error (red status line identifying the failed sensor) and continue booting to the main UI.

If either sensor fails initialization, the environmental sensing subsystem SHALL operate in an offline mode: the sampling task MUST NOT start, and the firmware MUST NOT proceed with partial telemetry.

The pin assignments SHALL be configurable via menuconfig fields so lab hardware can override the defaults without patching code.

#### Scenario: Hardware fault surfaces during boot
- **GIVEN** the FireBeetle SDA line is disconnected at power-on
- **WHEN** `app_main` attempts to create the AHT20 handle via the shared bus
- **THEN** the splash screen shows a sensor init error identifying the AHT20 failure in red
- **AND** the device continues booting and loads the thermostat UI
- **AND** the environmental sensing subsystem remains offline (no sampling task, no telemetry).

### Requirement: Availability Lifecycle Signaling
The environmental service SHALL publish retained availability payloads (`"online"` / `"offline"`) for each sensor's availability topic once MQTT reports ready.

If the sensors are successfully initialized at boot, the service MUST publish retained `"online"` for each sensor availability topic before publishing the next samples.

If either sensor fails initialization at boot, the service MUST publish retained `"offline"` for each environmental entity's per-sensor availability topic and MUST NOT attempt automatic reinitialization until the next reboot.

When a sensor accumulates `CONFIG_THEO_SENSOR_FAIL_THRESHOLD` (default 3) consecutive read failures after previously being online, the service MUST publish `"offline"` for that sensor and attempt reinitialization on each subsequent poll until success. A successful read or reinit resets the failure counter and publishes `"online"` before pushing the next sample. Shutdown paths also publish `"offline"` for all sensors before tearing down the bus.

#### Scenario: Transient error does not flip availability
- **GIVEN** the AHT20 fails a single read but succeeds on the next poll
- **WHEN** the failure counter is below the threshold
- **THEN** availability remains `"online"` and no availability message is published.

#### Scenario: Repeated failures mark sensor offline
- **GIVEN** the BMP280 fails 3 consecutive reads (matching the default threshold)
- **WHEN** the service increments the failure counter past the threshold
- **THEN** it publishes retained `"offline"` to the BMP280 availability topic
- **AND** attempts reinit on the next poll interval.

#### Scenario: Sensor recovery after offline
- **GIVEN** the AHT20 was marked offline after repeated failures
- **WHEN** reinit or read succeeds
- **THEN** the service resets the failure counter, publishes `"online"`, and resumes normal telemetry.

#### Scenario: Boot-time init failure marks entities unavailable
- **GIVEN** either AHT20 or BMP280 initialization fails during boot
- **WHEN** MQTT reports ready
- **THEN** the service publishes retained `"offline"` for all environmental per-entity availability topics
- **AND** Home Assistant marks the environmental entities unavailable even if stale retained state exists.

### Requirement: Home Assistant Discovery Config
On boot, the firmware SHALL publish retained discovery payloads for the four environmental sensors to `homeassistant/sensor/<Slug>/<object_id>/config`, regardless of whether sensor hardware initialization succeeds.

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
