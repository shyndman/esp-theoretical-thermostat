# Codebase Concerns

**Analysis Date:** 2026-02-08
**Updated:** 2026-02-09 (owner-prioritized)

## Immediate Concerns

### Memory Allocation Patterns (Internal RAM)
**Issue:** Hot paths still allocate/free frequently from the default heap, and on this target that means internal 8-bit RAM for `malloc/calloc` paths.
- **Files:**
  - `main/connectivity/mqtt_dataplane.c` lines 449-460, 669-676
  - `main/streaming/webrtc_stream.c` lines 640-645
- **Why immediate:** Internal RAM headroom is critical for system stability (tasks, network stack, LVGL, media).
- **Impact:** Fragmentation and allocation failures are more likely under bursty MQTT traffic or repeated WHEP negotiation attempts.
- **Current direction:** Replace per-fragment/per-message dynamic allocation in MQTT reassembly with serialized handling + fixed pre-allocated buffer and hard size cap.

### Task Stack Sizes
**Issue:** Stack budgets are tight relative to feature complexity and failure-path depth.
- **Current capacity:**
  - MQTT dataplane: 8192 bytes
  - Environmental sensors: 8192 bytes
  - WebRTC worker: 4096 bytes
  - Radar start: 6144 bytes (temporary)
- **Impact:** Stack overflow risk if message handling or nested error paths grow.
- **Priority:** Immediate
- **Next step:** Add stack high-water telemetry (`uxTaskGetStackHighWaterMark`) and define minimum safety thresholds per task.

### Heap Fragmentation Monitoring
**Issue:** Heap logging exists, but fragmentation monitoring is not yet actionable at runtime.
- **Current state:** Free/min/largest block values are logged periodically (`main/app_main.c`), but no per-subsystem attribution or alert thresholds.
- **Impact:** Fragmentation can degrade reliability before total free heap looks critical.
- **Priority:** Immediate
- **Next step:** Track largest-block trend and add warning thresholds tied to allocation-critical subsystems.

## Important, Not Immediate

### MQTT Reassembly Memory Pressure
**Issue:** Fragment reassembly can create transient and retained heap pressure.
- **How it allocates today:**
  - On each `MQTT_EVENT_DATA`, fragment data is copied into a newly allocated `malloc(event->data_len)` buffer.
  - First fragment of a message may also allocate topic storage.
  - Reassembly then allocates a second buffer `calloc(total_len + 1)` per in-flight message slot.
- **Files:** `main/connectivity/mqtt_dataplane.c` lines 449-460, 631-733
- **Why this matters:** Even when payload importance is low, the allocation pattern itself can pressure/fragment internal RAM during bursts.
- **Worst case shape:** Queue depth (`20`) can temporarily hold many allocated fragment copies while up to `MQTT_DP_MAX_REASSEMBLY` (`4`) in-flight payload buffers coexist.
- **Mitigation path:** Single active fragmented message, pre-allocated reassembly buffer, hard maximum payload size, and explicit drop/log on oversize/out-of-order traffic.

### LVGL Lock Contention
**Problem:** UI updates are serialized via `esp_lv_adapter_lock()`.
- **Files:** heavy usage in `main/connectivity/mqtt_dataplane.c`
- **Status:** Not an immediate concern for current deployment.
- **Improvement path (deferred):** Batch view-model updates or coalesce UI refresh points if contention shows up in profiling.

### Complex State Machine in Splash Screen
**Issue:** `ui_splash.c` remains a large and intricate state machine.
- **Files:** `main/thermostat/ui_splash.c` (1091 lines)
- **Status:** Not immediately important.
- **Impact:** Higher maintenance cost and increased regression risk during animation/transition edits.
- **Fix approach:** Refactor toward explicit state enum + transition table when this module is actively modified.

## Known Bugs

### Radar Start Timeout Race Condition
**Issue:** Radar init timeout path can outlive task/context ownership assumptions.
- **Files:** `main/app_main.c` lines 188-241
- **Trigger:** Slow I2C bus or unresponsive radar sensor
- **Risk:** Resource leak or unsafe lifetime interaction in timeout scenarios

### WebRTC Camera Frame Rate Configuration Failures Ignored
**Issue:** Camera FPS configuration failures are logged but not propagated as errors.
- **Files:** `main/streaming/webrtc_stream.c` lines 305-339
- **Trigger:** Camera driver rejecting `VIDIOC_S_PARM`
- **Workaround:** System continues with default camera frame rate

## Security Considerations

### Wi-Fi Credentials in SDK Configuration
**Risk:** Wi-Fi SSID and password can be stored in tracked `sdkconfig` values.
- **Files:** `main/Kconfig.projbuild` lines 4-14
- **Current mitigation:** AGENTS guidance recommends environment overrides
- **Recommendation:** Move runtime credentials to protected storage (NVS/provisioning flow) and avoid committing populated `sdkconfig` secrets.

## Fragile Areas

### WebRTC Stream Lifecycle Management
**Files:** `main/streaming/webrtc_stream.c`
**Why fragile:**
- Complex state interaction across start/stop/restart and request handling
- Manual resource lifecycle sequencing for capture + signaling
- Race surface between stop requests and in-flight negotiations
- **Status:** Not immediately important.
**Safe modification:** Keep mutex discipline (`s_state_mutex`) and minimize behavioral changes per patch.

### MQTT Dataplane State Synchronization
**Files:** `main/connectivity/mqtt_dataplane.c`
**Why fragile:**
- View model updates are asynchronous to UI lifecycle
- Ready/init flags can be observed across task boundaries
**Safe modification:** Prefer explicit synchronization around shared readiness/lifecycle gates.

## Missing Critical Features

### Runtime Configuration Persistence
**Gap:** Wi-Fi credentials, MQTT host, and network settings are not persisted via runtime configuration workflows.
**Blocks:** End-user provisioning and no-rebuild reconfiguration
**Priority:** High for production-style deployments

## Test Coverage Gaps

### Error Path Testing
**What's not tested:**
- Camera initialization failure recovery (`main/streaming/webrtc_stream.c`)
- I2C bus error handling in sensor reads (`main/sensors/env_sensors.c`)

**Risk:** Latent bugs can surface only during real-world failures

**Priority:** High

### Concurrent Access Patterns
**What's not tested:**
- Multiple simultaneous MQTT subscription updates
- WebRTC session teardown during active streaming
- Touch input during antiburn overlay

**Risk:** Race conditions in state management

**Priority:** High

---

*Concerns audit: 2026-02-08; owner-prioritized update: 2026-02-09*
