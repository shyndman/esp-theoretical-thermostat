# wake-radar-bluetooth

## Summary
Add an MQTT-triggered, self-expiring Bluetooth service window for the LD2410C radar so installers can temporarily enable Bluetooth pairing (`radar_bt` command) without manually rebooting or leaving the radio on indefinitely.

## Motivation
- The LD2410C ships with Bluetooth enabled, but in production we prefer it off except during provisioning to reduce interference and power draw.
- Installers occasionally need to tweak radar zones via the vendor mobile app; today that requires physical access or permanent Bluetooth enablement.
- A timed MQTT command lets support staff open a 15-minute window remotely, then automatically return to the default secure state.

## Scope
### In Scope
1. Define and document the `radar_bt` command on `<TheoBase>/command`.
2. Add radar-side bookkeeping to enable Bluetooth, keep it on for 15 minutes, auto-disable afterward, and refresh the timer on repeated commands.
3. Logging + manual test instructions so we can verify the behavior remotely.

### Out of Scope
- Changing the radar’s default Bluetooth posture at boot (Scott expects to configure that via the phone app if needed).
- Reporting window status via MQTT telemetry (could be added later if desired).
- Additional radar configuration commands.

## Approach
1. Extend `mqtt_dataplane` command parsing with a new payload `radar_bt`, logging receipt and delegating to the radar module.
2. Expose `radar_presence_request_bt_window(seconds)` so only `radar_presence.c` touches the LD2410 UART. This function sets/refreshes the expiry timestamp and schedules an enable if Bluetooth is currently off.
3. Enhance the radar polling task with a tiny state machine that:
   - Calls `ld2410_request_BT_on()` once per request and logs success/failure.
   - Tracks `disable_deadline_us` and, when reached, issues `ld2410_request_BT_off()` (retrying on failure) with matching logs.
   - Handles repeated commands by simply extending the deadline without toggling the radio.
4. Document manual verification in `docs/manual-test-plan.md` (publish `radar_bt`, observe INFO/WARN logs, confirm expiry and refresh behavior).

## Spec Deltas
| Capability | Action |
|------------|--------|
| thermostat-connectivity | **ADDED** requirement describing the `radar_bt` Bluetooth service window command |
