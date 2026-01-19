## Context
Today the firmware publishes retained state topics (e.g., radar presence `ON`/`OFF`) and per-entity retained availability topics (sensor/radar subsystems publish `online`/`offline`). Home Assistant uses those per-entity availability topics to decide whether the entity is available.

This works when the firmware is still running and can publish `offline` (e.g., a sensor read fails repeatedly). It does not work for device disappearance (power loss or Wi-Fi loss): the entity availability topics remain stuck at the last retained `online`, so HA keeps showing the last retained state value forever.

## Goals / Non-Goals
- Goals:
  - Make Theo-published entities become unavailable in Home Assistant on device power loss / Wi-Fi loss.
  - Preserve existing per-sensor/per-subsystem availability behavior (hardware fault visibility).
  - Keep existing topic schemas for state and per-entity availability intact.
- Non-Goals:
  - Redesigning the MQTT topic layout.
  - Introducing HA `expire_after`.

## Decisions
### Decision: Device-level availability topic backed by MQTT LWT
- Device availability topic: `<TheoBase>/<Slug>/availability`.
- MQTT client config sets LWT using ESP-IDF v5.5.2 / esp-mqtt config struct fields:
  - `esp_mqtt_client_config_t cfg`
  - `cfg.session.last_will.topic = <device availability topic>`
  - `cfg.session.last_will.msg = "offline"`
  - `cfg.session.last_will.msg_len = 0` (NULL-terminated)
  - `cfg.session.last_will.qos = 0`
  - `cfg.session.last_will.retain = 1`
- On `MQTT_EVENT_CONNECTED`, publish retained `online` to the device availability topic.

Rationale: LWT is broker-side; it fires when the connection disappears unexpectedly, which covers power/Wi-Fi loss without requiring any firmware code to run during failure.

### Decision: Use HA `availability` array with `availability_mode="all"`
Each entity advertises two availability sources:
1) device availability (`<TheoBase>/<Slug>/availability`)
2) per-entity availability (existing topics)

`availability_mode="all"` means the entity is only available when both are `online`.

## Key Implementation Constraints
- `mqtt_manager` MUST NOT depend on `env_sensors_*` for slug/base topic normalization because MQTT starts before env sensors and env sensors already depends on mqtt_manager.
- The device availability topic builder must re-implement the minimal normalization logic needed to match the existing published topic conventions.

## Alternatives considered
- Only per-entity availability (status quo): does not solve device disappearance.
- Device-only availability: solves disappearance but loses sensor-level fault visibility.
- `expire_after`: can solve staleness but forces periodic state publishes and can be noisy for slow sensors.

## Risks / Trade-offs
- Discovery payload changes from `availability_topic` to `availability` array. Existing HA entities should update via retained configs, but in some setups you may need to delete and re-discover entities.
- LWT timing is broker-dependent; expected to flip offline after the broker detects keepalive timeout.

## Migration Plan
- Firmware publishes updated retained discovery configs on boot.
- If HA does not refresh entities:
  1) delete the device/entities in HA, or
  2) clear retained discovery topics (manual broker cleanup), then reboot device.

## Open Questions
- None.
