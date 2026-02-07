## 1. Implementation
- [ ] 1.1 Initialize Theo identity strings before MQTT starts
  - [ ] Add an identity-only init step in the env_sensors module (no I2C access). Suggested shape:
    - `esp_err_t env_sensors_init_identity(void)` (idempotent)
    - Builds: device slug, friendly name, Theo base topic.
    - Does NOT create I2C buses, does NOT touch AHT/BMP.
  - [ ] Call identity init early in `main/app_main.c` after the splash is created but before starting MQTT.
  - [ ] Ensure `env_sensors_get_theo_base_topic()` and `env_sensors_get_device_slug()` return non-empty strings after identity init.
  - [ ] Add a regression check for the known ordering hazard:
    - `main/connectivity/mqtt_dataplane.c` initializes the device command topic on MQTT connect.
    - If Theo base is empty at that moment, the command topic is disabled.
    - The identity init step MUST make that impossible.

- [ ] 1.2 Make env sensor boot stage non-fatal (keep red splash error)
  - [ ] In `main/app_main.c`, update the "Starting environmental sensors…" stage:
    - Call `env_sensors_start()` as today.
    - If it returns non-`ESP_OK`, DO NOT call the fatal boot failure helper (`boot_fail`).
    - Instead, show the error line in red on the splash (use the existing `thermostat_splash_show_error(...)` path) and continue boot.
    - Keep the rest of the boot stages and UI bring-up unchanged.
  - [ ] Confirm that subsequent boot status lines still render; the env error remains visible in history.

- [ ] 1.3 Implement env_sensors "offline mode" for boot-time init failure (all-or-nothing)
  - [ ] In `main/sensors/env_sensors.c`, restructure startup so MQTT publishing hooks still work even when hardware init fails.
  - [ ] Define explicit state flags in env_sensors (names are flexible, but the behavior is not):
    - identity initialized (strings built)
    - hardware init succeeded / failed
    - sampling task running (only when hw init succeeds)
    - boot-time init error code stored for logging/splash
  - [ ] On every boot, ensure the firmware publishes retained HA discovery config for all four env entities once MQTT is connected.
    - This MUST happen even if hardware init fails.
    - Do not rely on “first successful sensor read” to publish discovery.
  - [ ] On MQTT connected:
    - If boot-time hardware init failed: publish retained per-entity availability `offline` for all four env entities.
    - If boot-time hardware init succeeded: publish retained per-entity availability `online` for all four env entities.
  - [ ] If boot-time init failed:
    - Do not start the sampling task.
    - Do not publish any env `state` topics.
    - Do not attempt reinit until reboot (no periodic retry loop).
  - [ ] Preserve existing runtime behavior for sensors that were successfully initialized:
    - Sampling task continues.
    - Threshold-based offline/online behavior remains.

- [ ] 1.4 Ensure "already connected" and "connect later" cases both work
  - [ ] If `mqtt_manager_is_ready()` is true when env_sensors initializes, publish discovery/availability immediately.
  - [ ] Otherwise, register an MQTT event handler and publish on the first `MQTT_EVENT_CONNECTED`.
  - [ ] Ensure handler registration is idempotent (no duplicate publications on reconnect unless desired by spec).

## 2. Validation
- [ ] 2.1 `idf.py build`
- [ ] 2.2 Manual boot test with env sensors disconnected:
  - [ ] Splash shows env init error line in red
  - [ ] Device reaches main UI (no reboot loop)
  - [ ] Home Assistant shows env sensor entities as unavailable (retained `availability=offline`)
  - [ ] Device command topic remains subscribed (e.g., `<TheoBase>/command` still works)
- [ ] 2.3 Manual boot test with sensors present:
  - [ ] Env entities publish online availability and state updates as before

## 3. Notes for Reviewers
- The implementation MUST address the MQTT connect ordering hazard described in `design.md`.
- The implementation MUST not introduce periodic retry or partial telemetry.
