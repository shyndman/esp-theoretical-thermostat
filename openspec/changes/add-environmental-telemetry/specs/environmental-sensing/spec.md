## ADDED Requirements

### Requirement: Environmental Sensor Hardware Initialization
The firmware SHALL initialize an ESP-IDF I2C master bus dedicated to the onboard environmental sensors during boot, using FireBeetle 2 GPIO7 for SDA and GPIO8 for SCL by default (both at 3.3 V with existing external pull-ups). The build MUST vendor the `jack-ingithub/aht20^0.1.1` and `k0i05/esp_bmp280^1.2.7` components and create one handle per sensor over the shared bus. Sensor bring-up SHALL occur before the splash screen dismisses so any missing hardware aborts boot with a splash error instead of silently skipping telemetry. The pin assignments SHALL be configurable via menuconfig fields so lab hardware can override the defaults without patching code.

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

### Requirement: Telemetry Publication Trigger
Whenever the sampling task obtains fresh data and the MQTT client reports ready, the firmware SHALL enqueue a publish for each cached quantity (two temperatures, humidity, pressure) onto the Theo-owned MQTT namespace defined in `thermostat-connectivity`. Publishes MUST follow the hallway layout (`<TheoBase>/sensor/hallway-theostat/<object_id>/state`, QoS0, retain=true) and include only the numeric payload. Telemetry publishes SHALL NOT block the sampling loop; if MQTT is unavailable the service logs a warning and remembers the latest values so the next successful publish uses the newest sample.

#### Scenario: MQTT temporarily offline
- **GIVEN** wifi drops after the sensors have already produced readings
- **WHEN** the sampling task wakes and sees `mqtt_manager_is_ready() == false`
- **THEN** it skips publishing, logs `Telemetry publish skipped (MQTT offline)`, and retains the cached readings for the eventual reconnect publish burst.

### Requirement: Availability Lifecycle Signaling
The environmental service SHALL publish retained availability payloads (`"online"` / `"offline"`) for each sensor’s availability topic immediately after initialization succeeds. On any sensor read or init failure, it MUST publish `"offline"`, continue retrying on the normal sampling interval, and publish `"online"` as soon as the next attempt succeeds. Shutdown paths also publish `"offline"` before tearing down the bus so Home Assistant reflects device health without delay.

#### Scenario: Sensor recovered after failures
- **GIVEN** the AHT20 goes offline mid-run and the service publishes `"offline"`
- **WHEN** the next retry succeeds
- **THEN** the service emits `"online"` to the availability topic before pushing the next humidity/temperature samples.
