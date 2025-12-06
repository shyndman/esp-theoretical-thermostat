# environmental-sensing Specification

## Purpose
TBD - created by archiving change add-environmental-telemetry. Update Purpose after archive.
## Requirements
### Requirement: Environmental Sensor Hardware Initialization
The firmware SHALL initialize an ESP-IDF I2C master bus dedicated to the onboard environmental sensors during boot, using FireBeetle 2 GPIO7 for SDA and GPIO8 for SCL by default (both at 3.3 V with existing external pull-ups). The build MUST add `jack-ingithub/aht20^0.1.1` and `k0i05/esp_bmp280^1.2.7` as managed component dependencies and create one handle per sensor over the shared bus. Sensor bring-up SHALL occur before the splash screen dismisses; if either sensor fails initialization, boot aborts with a splash error identifying the failed sensor instead of proceeding with partial telemetry. The pin assignments SHALL be configurable via menuconfig fields so lab hardware can override the defaults without patching code.

#### Scenario: Hardware fault surfaces during boot
- **GIVEN** the FireBeetle SDA line is disconnected at power-on
- **WHEN** `app_main` attempts to create the AHT20 handle via the shared bus
- **THEN** the failure bubbles up before `thermostat_ui_attach()` runs
- **AND** the splash screen shows a sensor init error, preventing the UI from loading until the hardware fault is fixed or the pins are reconfigured.

### Requirement: Periodic Sampling Task
The firmware SHALL spawn a dedicated FreeRTOS task (or equivalent worker) once both sensors are initialized. The task MUST read the AHT20 temperature/humidity and BMP280 temperature/pressure every `CONFIG_THEO_SENSOR_POLL_SECONDS` (default 5 s, configurable range 1–600 s). Successful samples update four cached values (AHT20 temp, AHT20 humidity, BMP280 temp, BMP280 pressure). Failed reads SHALL log WARN (including the esp_err_t) and leave prior cached values untouched; repeated failures MUST NOT crash the task. The service SHALL expose per-sensor timestamps so debugging can tell when each stream last updated.

#### Scenario: Sensor recovers after transient error
- **GIVEN** the BMP280 read fails once with `ESP_FAIL`
- **WHEN** the task logs the failure and waits for the next poll interval
- **THEN** the following successful measurement updates the cached BMP280 temp/pressure along with a fresh timestamp, without requiring a reboot.

### Requirement: Sensor Failure Threshold Configuration
The firmware SHALL provide `CONFIG_THEO_SENSOR_FAIL_THRESHOLD` (default 3, range 1–10) controlling how many consecutive read failures trigger an availability state change to `offline`. Each successful read resets the per-sensor failure counter to zero. When the counter reaches the threshold, the service publishes `offline` and attempts sensor reinitialization on subsequent polls until success.

#### Scenario: Configurable failure threshold
- **GIVEN** an installer sets `CONFIG_THEO_SENSOR_FAIL_THRESHOLD=5`
- **WHEN** the AHT20 fails 4 consecutive reads
- **THEN** availability remains `online`
- **AND** the 5th consecutive failure triggers the `offline` publish.

### Requirement: Telemetry Publication Trigger
Whenever the sampling task obtains fresh data and the MQTT client reports ready, the firmware SHALL enqueue a publish for each cached quantity (two temperatures, humidity, pressure) onto the Theo-owned MQTT namespace defined in `thermostat-connectivity`. Publishes MUST follow the topic layout (`<TheoBase>/sensor/<Slug>-theostat/<object_id>/state`, QoS0, retain=true) and include only the numeric payload. Telemetry publishes SHALL NOT block the sampling loop; if MQTT is unavailable the service logs a warning and remembers the latest values so the next successful publish uses the newest sample.

#### Scenario: MQTT temporarily offline
- **GIVEN** wifi drops after the sensors have already produced readings
- **WHEN** the sampling task wakes and sees `mqtt_manager_is_ready() == false`
- **THEN** it skips publishing, logs `Telemetry publish skipped (MQTT offline)`, and retains the cached readings for the eventual reconnect publish burst.

### Requirement: Availability Lifecycle Signaling
The environmental service SHALL publish retained availability payloads (`"online"` / `"offline"`) for each sensor's availability topic once MQTT reports ready after successful sensor initialization. When a sensor accumulates `CONFIG_THEO_SENSOR_FAIL_THRESHOLD` (default 3) consecutive read failures, the service MUST publish `"offline"` for that sensor and attempt reinitialization on each subsequent poll. A successful read or reinit resets the failure counter and publishes `"online"` before pushing the next sample. Shutdown paths also publish `"offline"` for all sensors before tearing down the bus.

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

