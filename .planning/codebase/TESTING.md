# Testing Patterns

**Analysis Date:** 2026-02-08

## Test Framework

**Runner:** ESP-IDF Unity test framework

**Build System:**
- Test apps are separate ESP-IDF projects in `test_apps/` directories
- Component-level tests in `components/esp_http_server/test_apps/`
- No project-level unit test harness; relies on `idf.py build` and on-device validation

**Run Commands:**
```bash
idf.py build                    # Build main firmware
idf.py flash                    # Flash to device
idf.py monitor                  # View serial output for validation
idf.py -C test_apps flash       # Run component test apps
```

## Test File Organization

**Location:**
- Component tests: `components/{component}/test_apps/main/test_*.c`
- Example: `components/esp_http_server/test_apps/main/test_http_server.c`

**Naming:**
- Test source files: `test_{module}.c`
- Test cases: `TEST_CASE("Description", "[TAG]")`

**Structure:**
```
components/esp_http_server/
├── test_apps/
│   ├── CMakeLists.txt
│   └── main/
│       ├── CMakeLists.txt
│       └── test_http_server.c
```

## Test Structure

**Suite Organization:**
```c
#include "unity.h"
#include "test_utils.h"

TEST_CASE("Leak Test", "[HTTP SERVER]")
{
    httpd_handle_t hd[SERVER_INSTANCES];
    unsigned task_count;
    bool res = true;

    test_case_uses_tcpip();

    task_count = uxTaskGetNumberOfTasks();
    printf("Initial task count: %d\n", task_count);

    // Test implementation...
    
    TEST_ASSERT(res == true);
}
```

**Assertion patterns:**
- `TEST_ASSERT(condition)` - Basic assertion
- `TEST_ASSERT_EQUAL(expected, actual)` - Equality check
- `TEST_ASSERT_NOT_NULL(ptr)` - Null pointer check
- `ESP_ERROR_CHECK()` for fatal errors in tests

**Setup/Teardown:**
- Tests use local setup within each TEST_CASE
- Helper functions like `test_httpd_start()` for common initialization

## Test Types

**Unit Tests:**
- Limited to component-level (esp_http_server)
- Test URI wildcard matching, handler limits, memory leaks

**Example from `test_http_server.c`:**
```c
TEST_CASE("URI Wildcard Matcher Tests", "[HTTP SERVER]")
{
    struct uritest {
        const char *template;
        const char *uri;
        bool matches;
    };

    struct uritest uris[] = {
        {"/", "/", true},
        {"/path", "/path", true},
        {"/path/*", "/path/blabla", true},
        {}
    };

    struct uritest *ut = &uris[0];
    while(ut->template != 0) {
        bool match = httpd_uri_match_wildcard(ut->template, ut->uri, strlen(ut->uri));
        TEST_ASSERT(match == ut->matches);
        ut++;
    }
}
```

**Integration Tests:**
- Manual validation per `docs/manual-test-plan.md`
- On-device testing of:
  - MQTT dataplane and Home Assistant integration
  - WebRTC streaming (H.264 + Opus)
  - OTA update flow
  - Environmental sensor telemetry
  - LED notification sequences

**Hardware-in-the-Loop:**
- Requires actual ESP32-P4 hardware (FireBeetle 2 or Nano BSP)
- Tests verify:
  - Boot sequence and splash screen transitions
  - Touch input handling
  - Backlight fade and presence detection
  - Audio playback

## Manual Test Procedures

**Documented in `docs/manual-test-plan.md`:**

| Test Area | Validation Steps |
|-----------|-----------------|
| Dataplane | MQTT subscription acceptance, setpoint commands, payload handling |
| Remote Setpoint | Animation timing, burst handling, auto-sleep scheduling |
| Boot & UI | LED ceremony, splash fade, entrance animations, interaction blocking |
| OTA | Upload endpoint, HTTP error handling, partition validation |
| Camera Streaming | WebRTC negotiation, audio track validation, reconnection |
| LED Notifications | Quiet hours gating, color transitions, effect queuing |
| Environmental | Sensor initialization, MQTT publishing, HA discovery |
| Presence Hold | Wake distance, dwell timing, hold cap, ignore reset |
| Transport Monitor | SDIO stats, flow control, overlay rendering |

**Example validation snippet:**
```bash
# OTA test
scripts/push-ota.sh

# MQTT validation
mosquitto_sub -t "homeassistant/sensor/hallway/+" -v

# Camera stream check
curl http://go2rtc/api/streams | jq '.thermostat.tracks'
```

## Mocking

**Framework:** None used - hardware-dependent codebase

**Patterns:**
- Conditional compilation with Kconfig options to disable features
- Example: `CONFIG_THEO_RADAR_ENABLE=n` to skip presence sensor tests
- `CONFIG_THEO_CAMERA_ENABLE=n` to skip camera initialization

## Test Data and Fixtures

**Asset Generation:**
- Fonts: `scripts/generate_fonts.py` (from `assets/fonts/fontgen.toml`)
- Images: `scripts/generate_images.py` (from `assets/images/imagegen.toml`)
- Audio: `scripts/generate_sounds.py` (from `assets/audio/soundgen.toml`)

**Required assets must be generated before testing:**
```bash
scripts/generate_fonts.py       # Creates assets/fonts/*.c files
scripts/generate_images.py      # Creates assets/images/*.c files
scripts/generate_sounds.py      # Creates assets/audio/*.c files
```

## Coverage

**Requirements:** No enforced coverage targets

**Current gaps:**
- No automated test coverage for UI code (`thermostat/*.c`)
- No automated coverage for MQTT dataplane logic
- No automated coverage for sensor drivers
- WebRTC streaming tested manually only

**Testing approach:**
- Build verification: `idf.py build` with all feature combinations
- On-device validation per manual test plan
- Log analysis for success/failure determination

## Debugging Failed Tests

**Log analysis:**
- Use `ESP_LOGI()`, `ESP_LOGW()`, `ESP_LOGE()` levels
- Module-specific TAGs for filtering: `"mqtt_dp"`, `"backlight"`
- Timestamped boot stages: `[boot] stage_name done (elapsed ms)`

**Memory debugging:**
- Heap monitoring: `log_heap_caps_state()` in `app_main.c`
- Periodic heap logging every 400ms via `s_heap_log_timer`
- IRAM usage reporting: `scripts/report-iram-usage.mjs`

---

*Testing analysis: 2026-02-08*
