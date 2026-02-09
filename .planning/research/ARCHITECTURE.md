# Architecture Research

**Domain:** Embedded thermostat firmware reliability hardening (ESP-IDF, brownfield)
**Researched:** 2026-02-09
**Confidence:** HIGH

## Standard Architecture

### System Overview

```text
┌──────────────────────────────────────────────────────────────────────────────┐
│ Boot Orchestration (app_main)                                               │
├──────────────────────────────────────────────────────────────────────────────┤
│  ┌────────────────┐  ┌──────────────────┐  ┌──────────────────────────────┐ │
│  │ mqtt_dataplane │  │ sensors/streaming│  │ reliability/control plane    │ │
│  │ (data plane)   │  │ (producers)      │  │ (new, thin cross-cutting)    │ │
│  └───────┬────────┘  └────────┬─────────┘  └──────────────┬───────────────┘ │
│          │                    │                           │                 │
├──────────┴────────────────────┴───────────────────────────┴─────────────────┤
│                       FreeRTOS + ESP-IDF Runtime                             │
├──────────────────────────────────────────────────────────────────────────────┤
│  MQTT client/event loop | heap_caps | task APIs | timers | event groups     │
└──────────────────────────────────────────────────────────────────────────────┘
```

### Component Responsibilities

| Component | Responsibility | Typical Implementation |
|-----------|----------------|------------------------|
| `main/app_main.c` | Boot sequencing and fail/continue policy | Keep existing order; add reliability init early and boot-fence APIs for timeout-safe async starts |
| `main/connectivity/mqtt_dataplane.c` | MQTT subscribe, fragment intake, payload decode | Replace dynamic fragment malloc/reassembly with fixed-size slot pool + bounded copy + deterministic eviction/failure counters |
| `main/connectivity/device_telemetry.c` | Publish diagnostics to MQTT/HA | Extend with stack watermark, heap fragmentation ratio, and threshold alerts (diagnostic entities) |
| `main/sensors/radar_presence.c` + timeout wrapper in `app_main.c` | Radar init and frame polling | Add generation-token boot fence + event-group completion to eliminate timeout/use-after-free races |
| `main/streaming/webrtc_stream.c` | Camera pipeline lifecycle and WHEP gating | Emit reliability state (init failures, retry backoff, memory pressure hints) into control plane |
| `main/reliability/*` (new) | Shared reliability primitives only | Central event IDs, thresholds, health snapshots, and alert policy; no business logic migration |

## Recommended Project Structure

```text
main/
├── app_main.c                           # keep orchestrator; call reliability init and boot guards
├── connectivity/
│   ├── mqtt_dataplane.c                 # bounded reassembly and drop accounting
│   └── device_telemetry.c               # publish reliability metrics + alerts
├── sensors/
│   └── radar_presence.c                 # report init/online transitions to reliability plane
├── streaming/
│   └── webrtc_stream.c                  # report retries/failures to reliability plane
└── reliability/                         # new minimal cross-cutting layer
    ├── reliability_events.h             # event base + event IDs + payload structs
    ├── reliability_health.c             # central snapshot store (lock-protected)
    ├── reliability_thresholds.h         # Kconfig-backed thresholds
    ├── reliability_alerts.c             # hysteresis/debounce for alert transitions
    └── reliability_boot_fence.c         # generation tokens + event-group wait helpers
```

### Structure Rationale

- **`main/reliability/`:** adds one narrow control plane without refactoring existing module ownership.
- **In-place module edits:** `mqtt_dataplane`, `device_telemetry`, `app_main`, `radar_presence`, and `webrtc_stream` keep their current contracts, reducing blast radius.

## Architectural Patterns

### Pattern 1: Bounded Reassembly Pipeline (for MQTT)

**What:** Use fixed-size fragment slots and fixed payload buffers for `MQTT_EVENT_DATA` assembly; reject/evict deterministically when capacity is exhausted.
**When to use:** Any MQTT payload path where broker/client can emit fragmented data (`total_data_len` + `current_data_offset`).
**Trade-offs:** Predictable memory and no heap churn, but hard max payload size and explicit drop policy required.

**Example:**
```c
typedef struct {
  bool in_use;
  int msg_id;
  size_t total_len;
  size_t filled;
  char topic[160];
  uint8_t payload[CONFIG_THEO_MQTT_REASSEMBLY_MAX_BYTES + 1];
  int64_t first_seen_us;
} dp_slot_t;

static dp_slot_t s_slots[CONFIG_THEO_MQTT_REASSEMBLY_SLOTS];
```

### Pattern 2: Reliability Control Plane via Events + Snapshot

**What:** Producers publish reliability events; a single health store maintains latest snapshot and alert state.
**When to use:** Cross-cutting metrics (stack headroom, heap fragmentation, drops, retries) needed by telemetry and boot policy.
**Trade-offs:** Small extra indirection, but removes ad hoc cross-module coupling.

**Example:**
```c
ESP_EVENT_DECLARE_BASE(THEO_RELIABILITY_EVENT);
typedef enum {
  THEO_REL_EVT_STACK_SAMPLE,
  THEO_REL_EVT_HEAP_SAMPLE,
  THEO_REL_EVT_MQTT_REASSEMBLY_DROP,
  THEO_REL_EVT_RADAR_INIT_TIMEOUT,
} theo_rel_event_id_t;
```

### Pattern 3: Generation-Token Boot Fence (timeout race safety)

**What:** Every async init attempt gets a monotonic generation ID; completions are accepted only if generation matches active request.
**When to use:** Timeout-based init wrappers (like radar start) where worker may finish after caller has timed out.
**Trade-offs:** Slightly more state bookkeeping, but removes stale completion and use-after-free classes.

## Data Flow

### Request Flow

```text
MQTT_EVENT_DATA
    ↓
mqtt_dataplane callback → fixed-slot reassembler → topic parser → UI/view-model update
                         ↓
                  reliability event (drop/oversize/timeout)
                         ↓
                 reliability snapshot + alert evaluator
                         ↓
              device_telemetry publishes diagnostic state
```

### State Management

```text
Task APIs + heap APIs
    ↓ (periodic sampler task)
reliability_health snapshot store
    ↓
device_telemetry + logs + HA diagnostics
```

### Key Data Flows

1. **Stack headroom telemetry:** `uxTaskGetStackHighWaterMark2()` per critical task -> convert words to bytes -> threshold classifier -> MQTT diagnostics/alerts.
2. **Heap fragmentation telemetry:** `heap_caps_get_info()` (`largest_free_block`, `total_free_bytes`, `minimum_free_bytes`) -> fragmentation ratio + trend -> warning/critical alert states.
3. **Radar timeout safety:** `app_main` requests `radar_presence_start` with generation token -> waits on event-group bits with timeout -> accepts completion only when generation still active.
4. **Streaming resilience signal:** `webrtc_stream` emits retry/backoff/failure counters -> reliability snapshot -> published diagnostics for field triage.

## Build Order By Dependency

1. **Reliability primitives first (`main/reliability/*`)**
   Define event IDs, snapshot structs, threshold config, and boot-fence API before touching producers.
2. **Boot fence integration in `app_main.c` + radar wrapper**
   Removes timeout race risk early; low dependency on the rest of telemetry.
3. **MQTT bounded reassembly in `mqtt_dataplane.c`**
   Largest memory-behavior change; depends on reliability event definitions for drop accounting.
4. **Telemetry expansion in `device_telemetry.c`**
   Consume stack/heap snapshots and publish alert entities after producers emit stable metrics.
5. **Streaming and sensors reliability emitters (`webrtc_stream.c`, `radar_presence.c`)**
   Add counters/signals last to avoid blocking core hardening outcomes.

## Scaling Considerations

| Scale | Architecture Adjustments |
|-------|--------------------------|
| 1 device (current) | Keep single reliability snapshot in RAM and publish low-rate diagnostics (no history store). |
| 10-100 devices | Standardize topic schema for reliability metrics and alerts to enable fleet dashboards. |
| 1000+ devices | Add backend-side aggregation/alert suppression; firmware stays event+snapshot model. |

### Scaling Priorities

1. **First bottleneck:** MQTT payload bursts causing temporary memory pressure; fix with bounded reassembly and explicit drop counters.
2. **Second bottleneck:** Long-tail stack/heap regressions hidden until field failures; fix with continuous watermark and fragmentation telemetry.

## Anti-Patterns

### Anti-Pattern 1: Reliability Logic Scattered in Each Module

**What people do:** Each module invents its own thresholds, alert rules, and metric formatting.
**Why it's wrong:** Inconsistent semantics and impossible-to-compare diagnostics.
**Do this instead:** Keep one reliability control plane (`main/reliability/*`) and let modules only emit facts/events.

### Anti-Pattern 2: Dynamic Allocation in Hot Reassembly Paths

**What people do:** Allocate/free per MQTT fragment and per reassembly buffer.
**Why it's wrong:** Amplifies heap fragmentation and failure probability exactly during noisy broker conditions.
**Do this instead:** Pre-allocate fixed slots/buffers and enforce bounded payload policy.

## Integration Points

### External Services

| Service | Integration Pattern | Notes |
|---------|---------------------|-------|
| MQTT broker/Home Assistant | Existing topic model + added diagnostic entities | Keep QoS/retain aligned with current diagnostics style in `device_telemetry.c`. |
| WHEP clients (LAN) | Existing `webrtc_stream` request queue and session gate | Reliability should observe and report retries/timeouts, not alter signaling protocol. |

### Internal Boundaries

| Boundary | Communication | Notes |
|----------|---------------|-------|
| `mqtt_dataplane` ↔ `reliability_health` | `esp_event_post` with typed payload | Decouple parser/reassembler from alert policy. |
| `app_main` ↔ `reliability_boot_fence` | direct API + event-group wait bits | Prevent stale completion from timed-out async starts. |
| `device_telemetry` ↔ `reliability_health` | read-only snapshot API | Keep publish formatting out of control plane. |

## Sources

- ESP-IDF FreeRTOS stack watermark APIs (`uxTaskGetStackHighWaterMark`, `uxTaskGetStackHighWaterMark2`) and RAM usage guidance: https://docs.espressif.com/projects/esp-idf/en/v5.4.3/esp32c6/api-reference/system/freertos_idf, https://docs.espressif.com/projects/esp-idf/en/v5.4.3/esp32c6/api-guides/performance/ram-usage (HIGH)
- ESP-IDF heap info/fragmentation observability (`heap_caps_get_info`, `multi_heap_info_t`, local minimum monitor APIs): https://docs.espressif.com/projects/esp-idf/en/v5.4.3/esp32c6/api-reference/system/mem_alloc, https://docs.espressif.com/projects/esp-idf/en/v5.4.3/esp32c6/api-reference/system/heap_debug (HIGH)
- ESP-MQTT fragmented receive behavior (`MQTT_EVENT_DATA`, `current_data_offset`, `total_data_len`): https://docs.espressif.com/projects/esp-idf/en/v5.4.3/esp32c6/api-reference/protocols/mqtt (HIGH)
- ESP event loop data-copy semantics for decoupled control plane (`esp_event_post`, `esp_event_post_to`): https://docs.espressif.com/projects/esp-idf/en/v5.4.3/esp32c6/api-reference/system/esp_event (HIGH)
- Repository architecture and module boundaries: `main/app_main.c`, `main/connectivity/mqtt_dataplane.c`, `main/connectivity/device_telemetry.c`, `main/sensors/radar_presence.c`, `main/streaming/webrtc_stream.c` (HIGH)

---
*Architecture research for: ESP-IDF thermostat reliability hardening*
*Researched: 2026-02-09*
