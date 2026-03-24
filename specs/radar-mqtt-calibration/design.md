## Context

The thermostat already exposes a Theo-owned MQTT device command topic at `<TheoBase>/command`, implemented in `mqtt_dataplane.c`, with simple imperative payloads such as `rainbow` and `restart`. Separately, the radar subsystem already owns LD2410 lifecycle, cached state, UART polling, and radar-specific logging in `radar_presence.c`.

The vendored `cosmavergari/ld2410` component includes firmware-2.44 automatic threshold support via `ld2410_auto_thresholds(device, timeout_s)`. Vendor guidance and the upstream library both point to a 10-second clear-room window for starting this routine. The requested feature is intentionally narrow: accept an MQTT command that starts calibration and log what happened at launch time, without introducing a richer status/reporting protocol.

Constraints that shape the design:
- The command must remain fire-and-forget.
- The timeout must be fixed at 10 seconds and not be installer-configurable.
- Calibration launch logging must be sufficient to tell whether the command was requested, rejected, attempted, and accepted by the sensor.
- Existing backlight/presence behavior must remain unchanged during calibration.

## Goals / Non-Goals

**Goals:**
- Extend the existing Theo command topic with a `radar_calibrate` payload.
- Keep MQTT command parsing simple while moving radar-specific decisions into `radar_presence`.
- Start LD2410 automatic threshold calibration with a fixed 10-second timeout.
- Log calibration launch outcomes using wording that matches the truth of a fire-and-forget workflow.
- Preserve current runtime behavior for radar polling, backlight wake, and MQTT command handling.

**Non-Goals:**
- Waiting for calibration completion or polling final auto-threshold status.
- Publishing MQTT status, response, or result topics for calibration.
- Suspending presence-based wake, hold, or other backlight behavior during calibration.
- Adding a general-purpose radar control subsystem beyond this single operation.
- Making calibration timeout configurable.

## Decisions

### 1. Reuse the existing Theo command topic
The feature will use the existing `<TheoBase>/command` topic rather than adding a dedicated calibration topic or a Home Assistant entity command path.

Rationale:
- The repo already has a device-scoped imperative command channel.
- `radar_calibrate` matches the existing command model better than a new request/response topic family.
- This avoids inventing a second command surface for one operation.

Alternatives considered:
- Dedicated MQTT topic for radar calibration: rejected because it creates a parallel command convention for one command.
- Home Assistant command entity: rejected because the requirement is operational control, not HA-first UX.

### 2. Keep MQTT dataplane dumb; put calibration ownership in `radar_presence`
`mqtt_dataplane` will recognize `radar_calibrate` and forward it to a radar-owned entrypoint. The radar module will perform all validation, LD2410 calls, and logging.

Rationale:
- `radar_presence` already owns device state, online/offline knowledge, and UART interaction.
- This keeps MQTT parsing at one level of abstraction and prevents device-specific policy from leaking into the dataplane.
- It leaves room to factor out a radar-control module later if more radar commands appear, without paying that abstraction cost now.

Alternatives considered:
- Implement calibration directly in `mqtt_dataplane`: rejected because it mixes transport parsing with radar device policy and LD2410 command flow.
- Add a new `radar_control.{c,h}` module now: rejected because a single operation does not justify a new subsystem yet.

### 3. Treat the command as “attempt to start calibration now”
The system contract will be launch-only. A successful command means the device accepted the request and attempted to send the auto-threshold start command to the sensor.

Rationale:
- The user explicitly chose fire-and-forget behavior.
- Logging only launch semantics prevents the firmware from implying successful completion it did not verify.
- This keeps the implementation small and honest.

Alternatives considered:
- Poll until calibration completes and log final success/failure: rejected because it changes the command contract and adds orchestration complexity that is not wanted.
- Publish a status topic after launch: rejected because it introduces a richer API than required.

### 4. Fix the calibration timeout at 10 seconds
The radar module will always invoke `ld2410_auto_thresholds(..., 10)`.

Rationale:
- Vendor firmware guidance for the feature uses a 10-second clear-room window.
- The upstream library documentation describes the timeout as the leave-the-room window with a default of 10 seconds.
- A fixed value avoids configuration drift for a feature intended to be used occasionally by an informed operator.

Alternatives considered:
- Make timeout configurable via Kconfig or MQTT payload: rejected because the user explicitly does not want configurability and community evidence for better values is weak.
- Use a shorter/longer fixed timeout: rejected because there is no stronger evidence than the vendor/library guidance for 10 seconds.

### 5. Log admission and launch truth, not completion truth
Calibration logs will distinguish these moments:
- request received
- request rejected due to preconditions
- calibration starting with timeout/context
- command accepted by sensor or start failed

Rationale:
- These logs answer the operational question “what happened when I sent the command?”
- They avoid claiming that calibration completed or improved thresholds.
- They preserve useful debugging context without adding MQTT result plumbing.

Alternatives considered:
- Minimal single-line logging: rejected because it would not satisfy the need to understand what took place.
- Logging completion/result anyway: rejected because the implementation would not verify completion.

### 6. Leave other subsystem behavior untouched during calibration
No special suppression will be added for presence wake, idle hold, or backlight behavior while calibration is starting.

Rationale:
- The operator will run calibration only when the area is clear.
- Adding suppression creates more behavior coupling and more failure modes than the user wants.
- The feature should stay as close as possible to the current runtime model.

Alternatives considered:
- Temporarily disable wake/hold logic during the 10-second window: rejected by user preference and unnecessary for the intended workflow.

## Risks / Trade-offs

- [Calibration command accepted but final sensor-side routine later fails] → Mitigation: log the command as launch-only and avoid claiming completion.
- [Calibration requested while radar is offline or disabled] → Mitigation: reject early in `radar_presence` with explicit WARN/INFO logs describing the reason.
- [Future radar commands make `radar_presence` feel too command-oriented] → Mitigation: keep the entrypoint narrow now and extract a `radar_control` module later only when a real command family exists.
- [Operators expect MQTT-visible status or results] → Mitigation: document in proposal/design/tasks that this command is fire-and-forget and logs are the source of truth.
- [LD2410 command launch races with normal polling ownership] → Mitigation: implementation should preserve single-module ownership of device interaction and avoid moving LD2410 policy into MQTT code.

## Migration Plan

1. Extend the command parser to recognize `radar_calibrate` and forward to a radar-owned API.
2. Add the radar calibration API and implement launch-time validation/logging around `ld2410_auto_thresholds(..., 10)`.
3. Update the relevant capability specs so the new command contract and calibration behavior are described.
4. Validate behavior by sending the MQTT command in a controlled environment and confirming the expected launch logs appear.

Rollback strategy:
- Remove the `radar_calibrate` command handling and the radar calibration entrypoint. Existing command and radar behavior will continue unaffected because this feature does not alter normal telemetry or wake logic.

## Open Questions

- None. The timeout is locked at 10 seconds, the command is fire-and-forget, logs are the source of truth, and no presence suppression is required during calibration.
