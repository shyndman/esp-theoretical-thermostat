# Codebase Concerns

**Analysis Date:** 2026-02-08

## Tech Debt

### Hardcoded Hardware Configuration
**Issue:** Multiple hardware parameters are hardcoded as `#define` constants rather than Kconfig options, limiting portability to different board revisions.
- **Files:** 
  - `main/thermostat/thermostat_leds.c` lines 20-56: LED count (39), GPIO layout constants, timing parameters
  - `main/streaming/webrtc_stream.c` lines 79-84: Camera resolution (800x800), FPS (5), retry delays
  - `main/thermostat/ui_state.h` lines 90-99: UI layout geometry constants
- **Impact:** Porting to new hardware requires source code modifications and rebuilds
- **Fix approach:** Migrate hardware-specific constants to Kconfig.projbuild with defaults matching current FireBeetle 2 harness

### Complex State Machine in Splash Screen
**Issue:** `ui_splash.c` implements a sophisticated animation state machine with 1000+ lines for boot screen transitions.
- **Files:** `main/thermostat/ui_splash.c` (1091 lines)
- **Impact:** Exit sequence retry logic (lines 1070-1085) uses static retry counter that persists across restarts, could theoretically overflow
- **Fix approach:** Consider simpler sequential state pattern or refactor into explicit state enum with transition table

### Global UI State Management
**Issue:** Heavy reliance on global state variables for UI components creates tight coupling.
- **Files:**
  - `main/thermostat/ui_setpoint_view.c` lines 10-18: Static LVGL object handles
  - `main/thermostat/ui_top_bar.c` lines 7-14: Static widget references
  - `main/thermostat/ui_actions.c` lines 16-19: Static action bar objects
- **Impact:** Difficult to unit test, potential for null pointer dereferences if initialization order changes
- **Fix approach:** Encapsulate in context struct passed to functions, add runtime initialization verification

### Reassembly Buffer Management
**Issue:** MQTT message reassembly uses fixed-size static buffer without overflow protection on message ID exhaustion.
- **Files:** `main/connectivity/mqtt_dataplane.c` lines 175-179, 715-732
- **Impact:** Under extreme load with 4+ concurrent fragmented messages, slot exhaustion drops data silently
- **Fix approach:** Add metrics/logging for reassembly slot pressure, consider dynamic allocation with timeout-based eviction

### Memory Allocation Patterns
**Issue:** Frequent small allocations on event paths without pool allocation.
- **Files:**
  - `main/streaming/webrtc_stream.c` lines 640-645: WHEP request allocation per connection
  - `main/connectivity/mqtt_dataplane.c` lines 450-460: Per-message topic/data allocation
- **Impact:** Heap fragmentation over long uptime, potential allocation failure under memory pressure
- **Fix approach:** Implement fixed-size pools for hot-path allocations, add allocation failure telemetry

## Known Bugs

### Radar Start Timeout Race Condition
**Issue:** Radar initialization timeout mechanism doesn't properly cancel spawned task on timeout.
- **Files:** `main/app_main.c` lines 188-241
- **Trigger:** Slow I2C bus or unresponsive radar sensor
- **Workaround:** Timeout detection works but leaked task context may consume resources until stack exhaustion

### WebRTC Camera Frame Rate Configuration Failures Ignored
**Issue:** Camera FPS configuration failures are logged but not propagated as errors.
- **Files:** `main/streaming/webrtc_stream.c` lines 305-339
- **Trigger:** Camera driver rejecting VIDIOC_S_PARM
- **Workaround:** System continues with default camera frame rate, may cause A/V sync issues

## Security Considerations

### Wi-Fi Credentials in SDK Configuration
**Risk:** Wi-Fi SSID and password stored as Kconfig strings, visible in `sdkconfig` file.
- **Files:** `main/Kconfig.projbuild` lines 4-14
- **Current mitigation:** AGENTS.md documents "Keep secrets out of `sdkconfig` by using environment overrides"
- **Recommendations:** Add runtime Wi-Fi credential loading from encrypted NVS partition, deprecate Kconfig-based credentials

### Command Topic Authentication
**Risk:** MQTT command topics (`rainbow`, `heatwave`, `coolwave`, `sparkle`, `restart`) processed without authentication.
- **Files:** `main/connectivity/mqtt_dataplane.c` lines 597-621
- **Current mitigation:** Commands only processed after MQTT connection established (implies network access control)
- **Recommendations:** Add command signing or token-based authentication for production deployments

### Buffer Overflow in Topic Construction
**Risk:** Topic string building uses `snprintf` with calculated buffer sizes but lacks runtime bounds verification.
- **Files:** `main/sensors/env_sensors.c` lines 380-390
- **Current mitigation:** Static analysis shows sizes within limits for current config
- **Recommendations:** Add `ESP_RETURN_ON_FALSE` checks for truncation conditions

## Performance Bottlenecks

### LVGL Lock Contention
**Problem:** All UI updates require `esp_lv_adapter_lock()` which serializes access.
- **Files:** Heavy usage in `main/connectivity/mqtt_dataplane.c` (47 lock/unlock pairs)
- **Cause:** MQTT message processing runs in dedicated task, must acquire lock for every view model update
- **Improvement path:** Consider read-copy-update pattern for view model, batch UI updates

### Splash Screen Animation CPU Usage
**Problem:** Per-pixel static noise generation during antiburn overlay consumes CPU.
- **Files:** `main/thermostat/backlight_manager.c` lines 887-978
- **Cause:** `snow_xorshift32()` called for every pixel every frame (800x800 display = 640k iterations/frame)
- **Improvement path:** Use DMA-capable noise texture, reduce update rate, or use hardware overlay

### MQTT Reassembly Memory Pressure
**Problem:** Large MQTT payloads (discovery configs) allocate from heap during reassembly.
- **Files:** `main/connectivity/mqtt_dataplane.c` lines 669-676
- **Cause:** `calloc(1, state->total_len + 1)` for payloads up to 896 bytes (per `ENV_SENSORS_PAYLOAD_MAX_LEN`)
- **Improvement path:** Add static pool for common payload sizes, stream large payloads

## Fragile Areas

### WebRTC Stream Lifecycle Management
**Files:** `main/streaming/webrtc_stream.c` (1156 lines)
**Why fragile:**
- Complex state machine with 15+ boolean flags (lines 117-136)
- Manual reference counting for capture resources
- Race conditions possible between `webrtc_stream_stop()` and in-progress negotiations
**Safe modification:** Always acquire `s_state_mutex` before state changes, use explicit state enum instead of boolean flags
**Test coverage:** Manual hardware testing only, no automated coverage

### LED Animation Timing Dependencies
**Files:** `main/thermostat/thermostat_leds.c`
**Why fragile:**
- Animation timing depends on `esp_timer_get_time()` deltas without drift compensation
- Effect cancellation may leave hardware in intermediate state
- 39 hardcoded LED positions for specific physical layout
**Safe modification:** Add animation state logging, validate LED count matches hardware at runtime

### MQTT Dataplane State Synchronization
**Files:** `main/connectivity/mqtt_dataplane.c`
**Why fragile:**
- View model updates happen asynchronously from MQTT task
- `g_ui_initialized` flag checked without synchronization primitive (lines 953, 974, 999)
- Potential for stale pointer access if `g_view_model` accessed during teardown
**Safe modification:** Add reader-writer lock for view model, use atomic operations for ready flags

## Scaling Limits

### Task Stack Sizes
**Current capacity:**
- MQTT dataplane: 8192 bytes
- Environmental sensors: 8192 bytes
- WebRTC worker: 4096 bytes
- Radar start: 6144 bytes (temporary)

**Limit:** Stack overflow risk if MQTT message nesting increases or environmental sensor discovery payloads grow beyond current 896 byte limit.

**Scaling path:** Monitor `uxTaskGetStackHighWaterMark()`, add stack canaries in debug builds.

### MQTT Topic Length
**Current capacity:** 160 bytes (`MQTT_DP_MAX_TOPIC_LEN`)

**Limit:** Home Assistant discovery topics with long device slugs may exceed this limit.

**Scaling path:** Increase to 256 bytes, add truncation detection with error logging.

### WHEP Session Queue
**Current capacity:** 1 request (`s_whep_request_queue` at line 1063 of `webrtc_stream.c`)

**Limit:** Only one concurrent WebRTC negotiation supported.

**Scaling path:** Queue depth is intentional (hardware limits), but add explicit error response for rejected concurrent requests.

## Dependencies at Risk

### ESP-Hosted Driver Coupling
**Risk:** Hard dependency on ESP-Hosted SDIO transport for Wi-Fi.
- **Impact:** Wi-Fi functionality tied to coprocessor firmware version
- **Migration plan:** None currently - this is architectural for ESP32-P4 split-core design

### LVGL Version Lock
**Risk:** Pinned to LVGL 9.4 via `main/idf_component.yml`.
- **Impact:** Major version upgrades require significant UI code changes
- **Migration plan:** Track LVGL 9.x branch, evaluate 10.x compatibility quarterly

### FireBeetle 2 BSP Assumptions
**Risk:** Code assumes specific GPIO mappings from `bsp/esp32_p4_nano.h`.
- **Impact:** Board redesigns require code changes
- **Migration plan:** BSP abstraction layer exists but has leakage - audit all direct GPIO references

## Missing Critical Features

### Runtime Configuration Persistence
**Gap:** Wi-Fi credentials, MQTT host, and other network settings not persisted to NVS at runtime.
**Blocks:** End-user configuration without rebuild, Wi-Fi provisioning workflows
**Priority:** High for production deployments

### Heap Fragmentation Monitoring
**Gap:** No runtime tracking of heap fragmentation, only total free bytes logged.
**Blocks:** Predictive maintenance, early warning of allocation failures
**Priority:** Medium - add `heap_caps_get_largest_free_block()` trending

### Unit Test Framework
**Gap:** No unit tests for business logic; all testing is hardware-based.
**Blocks:** Safe refactoring, regression prevention
**Priority:** Medium - consider Unity framework for ESP-IDF

## Test Coverage Gaps

### Error Path Testing
**What's not tested:** 
- MQTT reassembly slot exhaustion (`main/connectivity/mqtt_dataplane.c` line 660)
- Camera initialization failure recovery (`main/streaming/webrtc_stream.c` line 406)
- I2C bus error handling in sensor reads (`main/sensors/env_sensors.c` line 553)

**Risk:** Error paths may have latent bugs that only surface in production failures

**Priority:** High - add fault injection testing

### Concurrent Access Patterns
**What's not tested:**
- Multiple simultaneous MQTT subscription updates
- WebRTC session teardown during active streaming
- Touch input during antiburn overlay

**Risk:** Race conditions in state management

**Priority:** High - add stress testing with randomized timing

### Hardware Abstraction Edge Cases
**What's not tested:**
- LED strip with fewer/more than 39 LEDs
- Camera resolutions other than 800x800
- Audio pipeline with missing MAX98357

**Risk:** Hardcoded assumptions break on hardware variations

**Priority:** Medium - add hardware capability detection and graceful degradation

---

*Concerns audit: 2026-02-08*
