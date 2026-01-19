### 0. Prerequisites / Context
- [ ] Confirm current behavior: retained state topics remain stale in HA during device power/Wi-Fi loss because no one publishes `offline`.
- [ ] Confirm current topics:
  - Env sensors state: `<TheoBase>/sensor/<Slug>/<object_id>/state`
  - Env sensors per-entity availability: `<TheoBase>/sensor/<Slug>/<object_id>/availability`
  - Radar state/availability: `<TheoBase>/<component>/<Slug>-theostat/<object_id>/state|availability`
  - New device availability: `<TheoBase>/<Slug>/availability`

## 1. Proposal Validation
- [ ] 1.1 Run `openspec validate report-device-availability --strict` and fix formatting/issues.

## 2. MQTT Manager: Device Availability via LWT
- [ ] 2.1 Add a device-availability topic builder in `main/connectivity/mqtt_manager.c`.
- [ ] 2.2 Topic building MUST NOT depend on `env_sensors_*` (MQTT starts before `env_sensors_start()` and env_sensors already depends on mqtt_manager).
- [ ] 2.3 Implement the same normalization rules used elsewhere:
  - `CONFIG_THEO_THEOSTAT_BASE_TOPIC`: trim whitespace, strip leading/trailing slashes; if empty/invalid, use `theostat`.
  - `CONFIG_THEO_DEVICE_SLUG`: lowercase; replace invalid chars with `-`; collapse multiple separators; trim leading/trailing `-`; if empty, use `hallway`.
- [ ] 2.4 Build and store `<TheoBase>/<Slug>/availability` in a static buffer (max ~160 chars).
- [ ] 2.5 Configure esp-mqtt client LWT (retained) before `esp_mqtt_client_init()` using ESP-IDF v5.5.2 API:
  - Config struct: `esp_mqtt_client_config_t`
  - Fields: `cfg.session.last_will.topic`, `cfg.session.last_will.msg`, `cfg.session.last_will.msg_len`, `cfg.session.last_will.qos`, `cfg.session.last_will.retain`
  - Set:
    - `cfg.session.last_will.topic = <device availability topic>`
    - `cfg.session.last_will.msg = "offline"`
    - `cfg.session.last_will.msg_len = 0` (NULL-terminated)
    - `cfg.session.last_will.qos = 0`
    - `cfg.session.last_will.retain = 1`
- [ ] 2.6 On every `MQTT_EVENT_CONNECTED`, publish retained `online` to the device availability topic.
  - Use `esp_mqtt_client_publish(client, topic, payload, len=0, qos=0, retain=1)` (matches current sensor publish style in this repo).
- [ ] 2.7 Add DEBUG/INFO logs that include the computed device availability topic and publish results.

## 3. Home Assistant Discovery: Move to `availability` Array
- [ ] 3.1 Update `main/sensors/env_sensors.c` discovery payload generator:
  - Remove `availability_topic`/`payload_available`/`payload_not_available` at the top-level.
  - Add `availability_mode":"all"`.
  - Add `availability` array with two entries:
    1) Device availability topic: `<TheoBase>/<Slug>/availability`
    2) Per-entity availability topic: `<TheoBase>/sensor/<Slug>/<object_id>/availability`
  - Each entry uses `payload_available":"online"` and `payload_not_available":"offline"`.
- [ ] 3.2 Update `main/sensors/radar_presence.c` discovery payload generator for BOTH entities (presence + distance):
  - Remove single-topic availability fields.
  - Add `availability_mode":"all"`.
  - Add `availability` array with two entries:
    1) Device availability topic: `<TheoBase>/<Slug>/availability`
    2) Per-radar-entity availability topic: `<TheoBase>/<component>/<Slug>-theostat/<object_id>/availability`
- [ ] 3.3 Ensure existing per-entity availability publishing remains retained so HA restarts still recover cleanly.

## 4. Manual Validation (MQTT + HA)
- [ ] 4.1 Baseline: with device online, verify retained `online` at `<TheoBase>/<Slug>/availability`.
- [ ] 4.2 Power loss:
  - Turn off the device.
  - Verify broker publishes retained `offline` to `<TheoBase>/<Slug>/availability` (LWT).
  - Verify HA entities become `unavailable` (not just stale values).
- [ ] 4.3 Wi-Fi loss:
  - Drop Wi-Fi while device remains powered.
  - Verify broker eventually publishes retained `offline` to `<TheoBase>/<Slug>/availability`.
  - Verify HA entities become `unavailable`.
- [ ] 4.4 Recovery:
  - Restore power/Wi-Fi.
  - Verify device publishes retained `online` on reconnect.
  - Verify HA entities return to `available` and state updates resume.
- [ ] 4.5 Note expected timing: LWT triggers after the broker detects keepalive timeout; it will not be instantaneous.
