# wake-radar-bluetooth Design

## Problem Statement
Installers occasionally need to pair the LD2410C radar module with a phone app over Bluetooth to tune zones. Normally the thermostat keeps the radar's Bluetooth radio off to save power/noise. Scott wants to send an MQTT command that turns Bluetooth on for approximately 15 minutes, then turns it back off automatically. The command must be repeatable (re-issuing the command should reset the timer) and it must not disrupt the existing radar presence polling loop.

## Existing Architecture Touchpoints
1. **MQTT Command Topic** – `mqtt_dataplane` already subscribes to `<TheoBase>/command` and routes string payloads such as `rainbow` or `restart` through `process_command()`.
2. **Radar Subsystem** – `radar_presence.c` owns the single `LD2410_device_t *`, spawns the polling task, and never exposes the UART handle elsewhere. The vendor driver (`ld2410_request_BT_on/off`) sends commands that wait for ACK frames on the exact same UART stream that the polling task consumes.
3. **Logging Expectations** – Command handlers log `Received <command>` today, so any new command should provide similar INFO/WARN breadcrumbs for remote debugging.

## Proposed Flow
### Command Ingestion
- Extend `process_command()` with a new payload `radar_bt`.
- When received, log `"radar_bt command received"` and call a new radar API: `esp_err_t radar_presence_request_bt_window(uint32_t seconds)` with `seconds=900` (15 minutes). The MQTT handler does no UART work itself.
- Any non-`ESP_OK` return (radar not started, queue full) results in a WARN log `"radar_bt command rejected: <err>"` so command publishers know to retry.

### Radar Bluetooth Window State Machine
All UART interaction stays inside `radar_presence.c` to avoid races with `ld2410_check()`.

```
IDLE (BT off)
  │ radar_bt request + ld2410_request_BT_on() success
  ▼
ACTIVE (BT on, disable_deadline set)
  ├─ radar_bt request → refresh disable_deadline (no UART transaction)
  └─ now >= disable_deadline → call ld2410_request_BT_off()
         ├─ success → return to IDLE
         └─ failure → stay in ACTIVE, retry next poll tick
```

Implementation mechanics:
1. Add module-level fields guarded by the existing radar mutex (or a lightweight spin-free structure accessed from the radar task): `static bool s_bt_window_active; static int64_t s_bt_disable_deadline_us; static bool s_bt_enable_pending; static bool s_bt_disable_pending;` etc.
2. `radar_presence_request_bt_window(seconds)` simply sets `s_bt_enable_pending=true` and `s_bt_disable_deadline_us = now + seconds*1e6`. If a window is already active, it only updates the deadline and sets a `s_bt_log_refresh` flag so the task emits a refresh log once.
3. The radar polling task already wakes every ~100 ms. After handling UART frames, it checks the pending flags:
   - If `s_bt_enable_pending` and Bluetooth currently off, call `ld2410_request_BT_on()`. On success: mark `s_bt_window_active=true`, clear the pending flag, log INFO with the ISO timestamp when it will auto-disable. On failure: log WARN and clear `s_bt_window_active`; leave the pending flag false so we don’t spin; the user must resend the MQTT command.
   - If `s_bt_window_active` and `esp_timer_get_time() >= s_bt_disable_deadline_us`, attempt `ld2410_request_BT_off()`. On success: log INFO and clear the active flag. On failure: log WARN and keep the window active so we retry on the next poll tick.
4. Because the radar task already owns the UART, this approach avoids new locks or task suspensions.

### Error Handling & Resilience
- **Radar offline at command time**: `radar_presence_request_bt_window` returns `ESP_ERR_INVALID_STATE`, causing the MQTT handler to log WARN and drop the request (callers can retry once the radar comes back online).
- **ld2410_request_BT_on/off failure**: logged as WARN from within the radar task. On failure to re-disable, we keep retrying every poll interval until success so Bluetooth can’t stay on forever because of transient UART hiccups.
- **Command storming**: Repeated `radar_bt` payloads simply update the expiry timestamp; no extra MQTT acknowledgements or UART traffic are generated.
- **Default posture**: We do not send BT-off at boot; the radar’s default state is whatever the installer configured via the phone app. The MQTT command strictly provides a timed override.

### Telemetry & Logging
- INFO logs for command receipt, window start, refresh, and end.
- WARN logs when a command is rejected (radar offline) or when enabling/disabling fails.
- Optional future work (not in scope) could publish the remaining BT window duration via MQTT for visibility, but that is explicitly deferred.

### End-to-End Command Sequence
```
MQTT Publisher ──"radar_bt"──▶ Broker ──MQTT_EVENT_DATA──▶ mqtt_dataplane_event_handler
      │                                                              │
      │                                                              ├─ enqueues DP_MSG_FRAGMENT
      │                                                              ▼
      │                                                   mqtt_dataplane_task reassembles payload
      │                                                              │
      │                                                              ├─ detects `<TheoBase>/command`
      │                                                              ├─ logs `radar_bt command received`
      │                                                              └─ calls radar_presence_request_bt_window(900)
      │                                                                          │
      │                                                                          ├─ validates radar started
      │                                                                          ├─ sets enable_pending flag
      │                                                                          └─ records new disable deadline
      │                                                                                     │
      ▼                                                                                     ▼
                                             radar_task loop checks flags each 100 ms tick
                                             ├─ if enable_pending → ld2410_request_BT_on()
                                             │        ├─ success → log start + set active flag
                                             │        └─ failure → log WARN; window inactive
                                             ├─ if active & command repeats → refresh deadline, log
                                             └─ if active & now >= deadline → ld2410_request_BT_off()
                                                      ├─ success → log end + clear active flag
                                                      └─ failure → log WARN; retry next tick
```

## Detailed API Shapes
### `radar_presence_request_bt_window(uint32_t seconds)`
```c
esp_err_t radar_presence_request_bt_window(uint32_t seconds)
{
  if (!s_started || seconds == 0) {
    return ESP_ERR_INVALID_STATE;
  }
  int64_t now = esp_timer_get_time();
  int64_t deadline = now + (int64_t)seconds * 1000000LL;

  if (xSemaphoreTake(s_state_mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
    return ESP_ERR_TIMEOUT;
  }
  if (!s_bt_window_active) {
    s_bt_enable_pending = true;  // instruct radar task to call ld2410_request_BT_on()
  } else {
    s_bt_refresh_log_pending = true;
  }
  s_bt_disable_deadline_us = deadline;
  xSemaphoreGive(s_state_mutex);
  return ESP_OK;
}
```

### Radar Task Helper
```c
static void radar_task(void *arg)
{
  while (true) {
    poll_ld2410_frames();      // existing presence parsing
    handle_bt_window();        // new helper below
    vTaskDelay(poll_interval);
  }
}

static void handle_bt_window(void)
{
  int64_t now = esp_timer_get_time();

  if (s_bt_enable_pending && !s_bt_window_active) {
    s_bt_enable_pending = false;
    if (ld2410_request_BT_on(s_radar_device)) {
      s_bt_window_active = true;
      ESP_LOGI(TAG, "radar_bt window active until %lld", s_bt_disable_deadline_us);
    } else {
      ESP_LOGW(TAG, "radar_bt enable failed");
      s_bt_disable_deadline_us = 0;
    }
  } else if (s_bt_refresh_log_pending) {
    s_bt_refresh_log_pending = false;
    ESP_LOGI(TAG, "radar_bt window refreshed; new expiry %lld", s_bt_disable_deadline_us);
  }

  if (s_bt_window_active && now >= s_bt_disable_deadline_us) {
    if (ld2410_request_BT_off(s_radar_device)) {
      s_bt_window_active = false;
      ESP_LOGI(TAG, "radar_bt window ended");
    } else {
      ESP_LOGW(TAG, "radar_bt disable failed; retrying");
    }
  }
}
```

All Bluetooth-control globals (`s_bt_window_active`, `s_bt_disable_deadline_us`, `s_bt_enable_pending`, `s_bt_refresh_log_pending`) live alongside the current cached radar state and are only mutated either by the radar task or while holding `s_state_mutex`, so no additional synchronization primitives are required.

## Failure / Edge Case Matrix
| Case | Expected Behavior | Notes |
|------|-------------------|-------|
| Command arrives before `radar_presence_start()` finishes | `radar_presence_request_bt_window` returns `ESP_ERR_INVALID_STATE`; MQTT handler logs WARN and does nothing else | Prevents UART access before init |
| Command arrives while radar offline (timeout condition) | Same as above; we explicitly require `s_online==true` before accepting | Avoids futile UART commands |
| `ld2410_request_BT_on()` times out | WARN log, `s_bt_window_active` stays false, caller must resend command once radar recovers | No auto retry to avoid hammering UART |
| Multiple commands while active | Only `s_bt_disable_deadline_us` updates and a single refresh log is emitted per call | Prevents unnecessary UART traffic |
| Disable command fails | WARN log, active flag remains true, disable retried each poll tick until success | Ensures radio eventually shuts off |
| Thermostat reboot mid-window | BT state reverts to whatever the radar persisted; no special handling required per Scott | Out of scope |

## Validation Plan
1. **MQTT Command Test** – Publish `radar_bt` via `mosquitto_pub -t <TheoBase>/command -m radar_bt`. Expect log trio: receipt (dataplane), activation (radar task), expiry after ~15 min. Repeat command before expiry and verify the refresh log shows a new timestamp ~15 min from the second command.  
2. **Failure Injection** – Temporarily disconnect the radar UART or power during a window to force `ld2410_request_BT_off` to fail; confirm WARN logs and retries until hardware returns.  
3. **Regression Commands** – Exercise existing LED and restart commands to ensure the new branch doesn’t impact them.  
4. **Manual Test Doc** – Update `docs/manual-test-plan.md` with the above steps so QA/support can reproduce the behavior after flashing new firmware.

## Alternatives Considered
1. **Direct UART access from mqtt_dataplane** – rejected due to complexity of sharing the UART (would require new locks or temporarily pausing `ld2410_check()` and risks losing presence frames mid-command).
2. **Dedicated radar command queue task** – overkill for a single BT command. If we later add more LD2410 configuration commands, we can evolve the “pending flag” approach into a queue without changing the command contract.
