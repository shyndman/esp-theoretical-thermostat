## Context
The firmware currently treats environmental sensor initialization as boot-critical, calling the shared boot failure path that shows a splash error and then reboots.
The device should still be usable (UI, MQTT dataplane, etc.) even when env sensors are missing or broken.

## Dependency / Docs Verification
This proposal is grounded against the versions currently pinned by this repo:
- ESP-IDF: v5.5.2 (see `idf.py --version` and `dependencies.lock`).
- Environmental sensor drivers (ESP Component Registry, pinned in `main/idf_component.yml` / `dependencies.lock`):
  - `k0i05/esp_ahtxx` v1.2.7 (AHT20 temperature/humidity)
  - `k0i05/esp_bmp280` v1.2.7 (BMP280 temperature/pressure)
- Home Assistant MQTT discovery/availability:
  - The discovery payload may use `availability` (a list) plus `availability_mode`.
  - `availability` MUST NOT be used together with `availability_topic`.

## Goals / Non-Goals
- Goals:
  - Boot completes and UI loads even if AHT20/BMP280 initialization fails.
  - Splash shows a red error line for env sensor init failure.
  - Home Assistant entities for environmental sensing are explicitly marked unavailable via retained `availability=offline`.
  - Environmental sensing remains all-or-nothing at boot: no partial telemetry.
  - No automatic recovery for boot-time missing sensors; a reboot is required to attempt init again.
- Non-Goals:
  - Changing runtime sampling/recovery semantics once the env subsystem has successfully initialized.
  - Adding new UI surfaces beyond the existing splash error line.
  - Providing a manual re-init command or periodic retry loop.

## Decisions
1) Treat env sensor init failure as a non-critical boot stage.
   - We keep the existing splash error affordance (red line), but we do not call the fatal boot failure path.

2) Make Home Assistant availability explicit on missing sensors.
   - On boot-time init failure, publish retained per-entity availability `offline` for all four env entities.
   - Publish (or ensure publishing of) retained HA discovery configs so the entities exist and are correctly wired.
   - This prevents stale retained `state` values from looking valid when sensors are not present.

3) Preserve consistent identity/topic derivation independent of env sensor hardware.
   - Device slug and Theo base topic MUST be available even when env sensors are missing so other subsystems can continue to publish/subscribe deterministically.

## Detailed Implementation Notes

### Boot sequencing and splash behavior
- `env_sensors_start()` may return an error (missing/failed hardware).
- The boot sequence MUST treat that return as non-fatal:
  - Show the environmental sensor init error on the splash using the existing red error affordance.
  - Continue boot stages and eventually load the main UI.

To satisfy “keep the status message in red” while still continuing boot, the intent is:
- The env failure is added as an error line (red) into the splash history.
- Subsequent boot stage status lines may continue in their normal color.
- Because the splash retains a visible history, the red failure line remains visible during the rest of boot.

### Avoiding downstream breakage (topic identity availability)
Several subsystems build topics using `env_sensors_get_theo_base_topic()` and `env_sensors_get_device_slug()` (e.g., command topic subscription and various HA discovery topics). Today those strings are initialized inside `env_sensors_start()`, which runs after MQTT startup.

This creates a race/ordering hazard:
- MQTT can connect before `env_sensors_start()` runs.
- `mqtt_dataplane` tries to subscribe to `<TheoBase>/command` on connect.
- If `<TheoBase>` is still empty, `mqtt_dataplane` disables the command topic for the remainder of the session.

To prevent this failure mode (and make “env sensors optional” safe), the identity strings MUST be initialized before MQTT can use them.

Recommended approach for implementation:
1) Add an identity-only initialization step to the env_sensors module (no I2C access).
2) Call it early in boot (after the splash exists, before starting MQTT).
3) Ensure `env_sensors_get_*()` returns non-empty strings after identity init, even if hardware init later fails.

### Home Assistant behavior when sensors are missing
When boot-time env init fails:
- Publish retained HA discovery config for all four env entities.
- Publish retained per-entity `availability=offline` for all four env entities.
- Do not start the sampling task.
- Do not attempt retry until reboot.

This prevents stale retained `state` topics (from a previous successful boot) from being interpreted as valid data.

### Concrete topic examples (slug=`hallway`, Theo base topic=`theostat`)
- Discovery configs:
  - `homeassistant/sensor/hallway/temperature_bmp/config`
  - `homeassistant/sensor/hallway/temperature_aht/config`
  - `homeassistant/sensor/hallway/relative_humidity/config`
  - `homeassistant/sensor/hallway/air_pressure/config`
- Per-entity availability (retained):
  - `theostat/sensor/hallway/temperature_bmp/availability` -> `offline`
  - `theostat/sensor/hallway/temperature_aht/availability` -> `offline`
  - `theostat/sensor/hallway/relative_humidity/availability` -> `offline`
  - `theostat/sensor/hallway/air_pressure/availability` -> `offline`

## Risks / Trade-offs
- If env sensors are missing, upstream automations that depend on temperature/humidity/pressure will see the entities as unavailable. This is intentional and preferable to stale values.
- Boot continues with reduced functionality; the splash error line is the main indication.

## Migration Plan
No migration steps required. Home Assistant entities remain the same object IDs and topics.

## Open Questions
None for this change proposal.
