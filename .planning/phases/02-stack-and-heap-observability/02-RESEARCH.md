# Phase 2: Stack and Heap Observability - Research

**Researched:** 2026-02-09
**Domain:** ESP-IDF runtime stack and internal-RAM heap observability with threshold alerting
**Confidence:** HIGH

## User Constraints

No phase `CONTEXT.md` exists yet. Constraints below are taken from the phase brief, roadmap, requirements, and prior project decisions in scope.

### Locked Decisions
- Treat stack/heap thresholds and alerting as required observability, not optional diagnostics.
- Keep architecture aligned with existing ESP-IDF + current telemetry/logging patterns from Phase 1.
- Internal RAM safety is first-class.
- Scope for this phase is OBS-01/02/03/04 only.

### Claude's Discretion
- Choose concrete threshold model (warning/critical bands, hysteresis, cooldown).
- Choose where to collect stack metrics (handle registry vs name lookup) as long as required tasks are covered.
- Choose how to compute heap-fragmentation risk from largest-free-block trends.
- Choose telemetry payload schema/topic/object IDs consistent with existing Home Assistant discovery patterns.

### Deferred Ideas (OUT OF SCOPE)
- Watchdog and panic/core-dump contract work (Phase 3).
- MQTT auth/signing and topic scaling refactors.
- Broader architecture rewrites unrelated to observability.

## Summary

ESP-IDF already provides all primitives needed for this phase without new third-party components: per-task stack high-watermark APIs (`uxTaskGetStackHighWaterMark()`/`uxTaskGetStackHighWaterMark2()`), capability-scoped heap metrics (`heap_caps_get_free_size`, `heap_caps_get_minimum_free_size`, `heap_caps_get_largest_free_block`, `heap_caps_get_info`), and allocation-failure hooks. Critically, on ESP-IDF these stack watermark APIs report **bytes** (not words), and Espressif explicitly recommends tracking largest free block alongside total free heap to detect fragmentation risk.

The repository already has the right integration points: `main/app_main.c` runs a periodic heap monitor timer and currently calls `mqtt_dataplane_periodic_tick()`, while `main/connectivity/device_telemetry.c` already publishes diagnostic entities via MQTT + Home Assistant discovery. Phase 2 should build on these two paths: keep low-cost periodic sampling in the existing timer cadence, and publish structured diagnostics through the existing telemetry/discovery pattern.

For the required task set (MQTT dataplane, sensors, WebRTC worker, radar-start path), the main planning risk is task-handle ownership and lifetime. Long-lived tasks can be sampled by stable handles. The radar-start path is short-lived and must self-report before delete/exit (or write into a retained metric slot) to avoid missing the metric window.

**Primary recommendation:** Implement a small runtime-health module that samples stack + internal-RAM heap on the existing timer cadence, drives threshold state machines (warn/critical with hysteresis), and publishes diagnostics through existing `device_telemetry`/HA discovery patterns.

## Standard Stack

### Core
| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| ESP-IDF FreeRTOS (IDF) task APIs | ESP-IDF v5.5.2 (IDF FreeRTOS based on v10.5.1) | Per-task stack high watermark (`uxTaskGetStackHighWaterMark2`) | Official API, byte-based result on ESP-IDF, no custom instrumentation needed |
| ESP-IDF heap capabilities APIs (`esp_heap_caps`) | ESP-IDF v5.5.2 | Internal-RAM heap metrics (`free`, `min`, `largest`, aggregate info) | Capability-aware and explicitly supports fragmentation visibility |
| Existing project telemetry path (`device_telemetry` + `ha_discovery`) | In-repo | Publish observability metrics and alert entities | Already used for diagnostics; matches current architecture |

### Supporting
| Library | Version | Purpose | When to Use |
|---------|---------|---------|-------------|
| `esp_timer` periodic callback in `app_main` | ESP-IDF bundled | Periodic sampling trigger | Reuse existing heap monitor callback cadence |
| `esp_log` | ESP-IDF bundled | Local warning/critical evidence and rate-limited diagnostics | Always, especially when MQTT is temporarily unavailable |
| `heap_caps_register_failed_alloc_callback()` | ESP-IDF v5.5.2 | Optional escalation signal when allocations fail | Use as corroborating critical-memory signal |

### Alternatives Considered
| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| Direct per-task watermark reads for required tasks | `uxTaskGetSystemState()` snapshots for all tasks | Easier global scan but debug-oriented and can suspend scheduler for extended period |
| Production metric path via low-cost heap APIs | Heap Task Tracking | Official docs call it debug-oriented with non-negligible RAM/perf overhead |
| Internal-RAM scoped metrics (`MALLOC_CAP_INTERNAL`) | Global heap (`esp_get_free_heap_size`) only | Misses internal-RAM pressure and fragmentation risk signal |

**Installation:**
```bash
# No new packages required; use existing ESP-IDF and in-repo telemetry modules
```

## Architecture Patterns

### Recommended Project Structure
```
main/
├── connectivity/runtime_health.[ch]   # stack+heap sampling, thresholds, alert state machine
├── connectivity/device_telemetry.c    # discovery + metric/alert state publish
├── connectivity/mqtt_dataplane.[ch]   # expose dataplane task handle/metric hooks
├── sensors/env_sensors.c              # expose sensor task handle/metric hooks
└── app_main.c                         # existing periodic timer callback invokes runtime health tick
```

### Pattern 1: Per-Task Stack Probe Registry (Handle-Based)
**What:** Maintain a fixed registry of required probes (`mqtt_dataplane`, `env_sensors`, `webrtc_worker`, `radar_start_path`) and sample each by task handle.
**When to use:** For OBS-01 metric collection on each periodic tick.
**Example:**
```c
// Source: ESP-IDF FreeRTOS docs
// https://docs.espressif.com/projects/esp-idf/en/v5.5.2/esp32p4/api-reference/system/freertos_idf.html
configSTACK_DEPTH_TYPE free_bytes = uxTaskGetStackHighWaterMark2(task_handle);
size_t used_bytes = stack_size_bytes - (size_t)free_bytes;
```

### Pattern 2: Radar-Start Self-Report for Ephemeral Task Path
**What:** The short-lived radar-start task records its own post-peak watermark before exit (`uxTaskGetStackHighWaterMark2(NULL)`) into a retained metric slot.
**When to use:** For OBS-01 coverage of radar-start path that may finish before periodic observer sees it.
**Example:**
```c
// Source: ESP-IDF FreeRTOS docs (xTask NULL means current task)
// https://docs.espressif.com/projects/esp-idf/en/v5.5.2/esp32p4/api-reference/system/freertos_idf.html
runtime_health_record_radar_start_hwm((size_t)uxTaskGetStackHighWaterMark2(NULL));
vTaskDelete(NULL);
```

### Pattern 3: Internal-RAM Heap Health Snapshot
**What:** Sample internal RAM only (`MALLOC_CAP_INTERNAL`) for free/min/largest metrics and derive fragmentation indicators from largest-free-block behavior.
**When to use:** Every periodic tick for OBS-03/04.
**Example:**
```c
// Source: ESP-IDF Heap Allocation + Heap Debugging docs
// https://docs.espressif.com/projects/esp-idf/en/v5.5.2/esp32p4/api-reference/system/mem_alloc.html
// https://docs.espressif.com/projects/esp-idf/en/v5.5.2/esp32p4/api-reference/system/heap_debug.html
size_t free_b = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
size_t min_b = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
size_t largest_b = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
float frag_ratio = (free_b > 0) ? ((float)largest_b / (float)free_b) : 0.0f;
```

### Pattern 4: Threshold State Machine with Hysteresis
**What:** Maintain per-signal state (`OK`, `WARN`, `CRIT`) and transition only when threshold + hysteresis conditions hold for N consecutive samples.
**When to use:** OBS-02 and OBS-04 alerting to avoid flapping.
**Example:**
```c
typedef enum { HEALTH_OK, HEALTH_WARN, HEALTH_CRIT } health_level_t;

if (headroom_bytes <= critical_threshold) {
  promote_after_n("stack.mqtt_dataplane", HEALTH_CRIT, samples_crit);
} else if (headroom_bytes <= warning_threshold) {
  promote_after_n("stack.mqtt_dataplane", HEALTH_WARN, samples_warn);
} else if (headroom_bytes >= clear_threshold) {
  demote_after_n("stack.mqtt_dataplane", HEALTH_OK, samples_clear);
}
```

### Anti-Patterns to Avoid
- **Sampling by task name on every short interval:** `xTaskGetHandle()` is relatively slow and should be used sparingly.
- **Using only total free heap as health:** misses fragmentation; docs explicitly require largest-free-block co-monitoring.
- **Global heap metrics for this phase:** OBS-03 requires internal-RAM risk visibility, so use `MALLOC_CAP_INTERNAL`.
- **No hysteresis/cooldown in alerts:** causes WARN/CRIT oscillation under normal bursty load.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Stack usage instrumentation | Custom stack scanning or canary walkers | `uxTaskGetStackHighWaterMark2()` / `vTaskGetInfo()` | Official per-task metric with IDF semantics in bytes |
| Heap state introspection | Custom allocator metadata parser | `heap_caps_get_*` and `heap_caps_get_info()` | Capability-aware and maintained by ESP-IDF |
| Production per-allocation accounting | Always-on heap tracing/task-tracking pipeline | Low-cost periodic metrics + optional debug-only tracing | Official docs warn heap task tracking has significant overhead |
| Fragmentation detector internals | Custom multi-heap internals traversal | `largest_free_block` + `free_bytes` trend | ESP-IDF docs recommend this signal pair directly |

**Key insight:** Use ESP-IDF primitives for raw measurements and hand-roll only the policy layer (thresholding, trend interpretation, alert lifecycle).

## Common Pitfalls

### Pitfall 1: Wrong stack watermark units
**What goes wrong:** Threshold math is incorrect by 4x if code assumes words instead of bytes.
**Why it happens:** Standard FreeRTOS docs often mention words; ESP-IDF returns bytes.
**How to avoid:** Normalize all stack metrics as bytes, and label configs/topics explicitly (`*_bytes`).
**Warning signs:** Impossible headroom values or mismatched percent calculations.

### Pitfall 2: Missing short-lived radar-start metric
**What goes wrong:** Radar-start path shows as unknown/zero because task exits before observer samples.
**Why it happens:** Ephemeral boot task lifetime is shorter than periodic cadence.
**How to avoid:** Self-report watermark from radar-start task before delete/return.
**Warning signs:** Radar-start metric absent while radar init clearly ran.

### Pitfall 3: Fragmentation blind spot
**What goes wrong:** Free heap looks healthy while large allocations fail.
**Why it happens:** Only `free_bytes` tracked; `largest_free_block` ignored.
**How to avoid:** Track both values and compute ratio/trend-based risk.
**Warning signs:** Allocation failures despite high total free heap.

### Pitfall 4: Flapping alerts
**What goes wrong:** WARN/CRIT alerts spam during transient spikes.
**Why it happens:** Single-sample threshold crossing without hysteresis.
**How to avoid:** Add consecutive-sample gating and clear thresholds above enter thresholds.
**Warning signs:** Alternating warning/clear logs within seconds.

### Pitfall 5: Debug features in production path
**What goes wrong:** Observability itself increases RAM pressure and jitter.
**Why it happens:** Enabling heap task tracking/tracing continuously.
**How to avoid:** Keep tracing/task tracking for debug builds only; production uses lightweight snapshot APIs.
**Warning signs:** Increased baseline RAM usage and timing regressions after enabling diagnostics.

## Code Examples

Verified patterns from official sources and current codebase:

### Per-task stack headroom metric (bytes)
```c
// Source: ESP-IDF FreeRTOS API + RAM usage guide
// https://docs.espressif.com/projects/esp-idf/en/v5.5.2/esp32p4/api-reference/system/freertos_idf.html
// https://docs.espressif.com/projects/esp-idf/en/v5.5.2/esp32p4/api-guides/performance/ram-usage.html
size_t stack_headroom_b = (size_t)uxTaskGetStackHighWaterMark2(task_handle);
```

### Internal-RAM heap snapshot
```c
// Source: ESP-IDF Heap APIs
// https://docs.espressif.com/projects/esp-idf/en/v5.5.2/esp32p4/api-reference/system/mem_alloc.html
multi_heap_info_t info = {0};
heap_caps_get_info(&info, MALLOC_CAP_INTERNAL);
// info.total_free_bytes, info.minimum_free_bytes, info.largest_free_block
```

### Existing periodic integration point in this repo
```c
// Source: main/app_main.c
static void heap_log_timer_cb(void *arg)
{
  (void)arg;
  mqtt_dataplane_periodic_tick(esp_timer_get_time());
  log_heap_caps_state("-periodic");
}
```

### Existing diagnostic publish pattern in this repo
```c
// Source: main/connectivity/device_telemetry.c
int msg_id = esp_mqtt_client_publish(client, topic, payload, 0, 0, 1);
```

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| Free-heap-only health checks | Free + minimum + largest-free-block + trend | ESP-IDF docs emphasize this in current guides | Detects fragmentation risk earlier |
| Ad-hoc stack sizing guesses | Runtime watermark-driven stack headroom tracking | Ongoing best practice in ESP-IDF 5.x docs | Makes stack risk measurable and actionable |
| Debug-heavy always-on heap tracing | Lightweight periodic metrics in production, tracing only for debug | Reinforced in current heap-debug docs | Lower overhead and fewer observer-induced regressions |

**Deprecated/outdated:**
- Treating total free heap as sufficient memory health signal.
- Assuming stack watermark APIs return words in ESP-IDF.

## Open Questions

1. **What exact warn/crit thresholds should ship for each required task?**
   - What we know: API gives byte-accurate headroom; required tasks are known.
   - What's unclear: Accepted risk budget per task under worst-case real workloads.
   - Recommendation: Start with conservative defaults (e.g., WARN 1024 B, CRIT 512 B for medium tasks; tune by captured peaks in manual tests).

2. **What trend window should define heap-fragmentation risk for alerts?**
   - What we know: Docs support largest-free-block vs total-free monitoring.
   - What's unclear: Best sample window for this firmware's burst pattern.
   - Recommendation: Use a short rolling window (5-10 samples at current timer cadence) plus ratio threshold and monotonic-drop condition.

3. **Should alert entities be binary sensors, numeric severity, or both?**
   - What we know: Existing HA discovery supports `sensor` and `binary_sensor`; diagnostics already publish there.
   - What's unclear: Operator preference for dashboarding and automations.
   - Recommendation: Publish both: numeric headroom/ratio sensors + binary WARN/CRIT entities for automations.

## Sources

### Primary (HIGH confidence)
- Context7 `/websites/espressif_projects_esp-idf_en_release-v5_5_esp32s3` - FreeRTOS watermark API semantics, heap capability APIs, `multi_heap_info_t`, heap-debug guidance.
- ESP-IDF FreeRTOS (IDF) v5.5.2: https://docs.espressif.com/projects/esp-idf/en/v5.5.2/esp32p4/api-reference/system/freertos_idf.html
- ESP-IDF Heap Memory Allocation v5.5.2: https://docs.espressif.com/projects/esp-idf/en/v5.5.2/esp32p4/api-reference/system/mem_alloc.html
- ESP-IDF Heap Memory Debugging v5.5.2: https://docs.espressif.com/projects/esp-idf/en/v5.5.2/esp32p4/api-reference/system/heap_debug.html
- ESP-IDF Minimizing RAM Usage v5.5.2: https://docs.espressif.com/projects/esp-idf/en/v5.5.2/esp32p4/api-guides/performance/ram-usage.html
- Repository integration points: `main/app_main.c`, `main/connectivity/device_telemetry.c`, `main/connectivity/mqtt_dataplane.c`, `main/sensors/env_sensors.c`, `main/streaming/webrtc_stream.c`

### Secondary (MEDIUM confidence)
- None required.

### Tertiary (LOW confidence)
- None required.

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH - Fully based on official ESP-IDF APIs and current repo modules.
- Architecture: HIGH - Derived from verified integration points and phase requirements.
- Pitfalls: HIGH - Grounded in official ESP-IDF caveats plus repository task-lifetime realities.

**Research date:** 2026-02-09
**Valid until:** 2026-03-11 (30 days; APIs stable, implementation details may shift)
