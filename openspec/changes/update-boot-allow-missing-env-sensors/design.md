## Context
The firmware currently treats environmental sensor initialization as boot-critical, calling the shared boot failure path that shows a splash error and then reboots.
The device should still be usable (UI, MQTT dataplane, etc.) even when env sensors are missing or broken.

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

## Risks / Trade-offs
- If env sensors are missing, upstream automations that depend on temperature/humidity/pressure will see the entities as unavailable. This is intentional and preferable to stale values.
- Boot continues with reduced functionality; the splash error line is the main indication.

## Migration Plan
No migration steps required. Home Assistant entities remain the same object IDs and topics.

## Open Questions
None for this change proposal.
