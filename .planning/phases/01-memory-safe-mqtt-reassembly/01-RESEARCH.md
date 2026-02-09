# Phase 1: Memory-Safe MQTT Reassembly - Research

**Researched:** 2026-02-09
**Domain:** ESP-IDF MQTT fragmented receive handling under fixed-memory constraints
**Confidence:** HIGH

## User Constraints

No phase `CONTEXT.md` exists yet. Constraints below are taken from `ROADMAP.md`, `REQUIREMENTS.md`, `STATE.md`, and project decisions provided in scope.

### Locked Decisions
- Prioritize bounded fragmented MQTT reassembly and serialized flow to cap internal-RAM pressure.
- Treat payload correctness as secondary to memory stability if needed.
- Keep scope tight; defer non-immediate concerns.

### Claude's Discretion
- Choose the concrete fixed buffer size(s), state machine shape, and counter schema.
- Choose where counters are surfaced first (logs-only vs lightweight exported status), as long as deterministic logs are present now.

### Deferred Ideas (OUT OF SCOPE)
- Stack/heap observability thresholds and alerting (Phase 2).
- Watchdog/panic/core-dump fault contract work (Phase 3).
- MQTT auth/signing and topic-length scaling refactors (explicitly out of scope in requirements).

## Summary

ESP-IDF's MQTT receive semantics already support fragmented inbound payloads via `MQTT_EVENT_DATA` with `total_data_len` and `current_data_offset`; only the first fragment carries topic metadata when a message is fragmented. This means a safe implementation should explicitly model a fragment-assembly state machine and must cache topic on first fragment. Relying on "topic always present" or using ad-hoc per-fragment allocation is a known failure mode.

Current project code in `main/connectivity/mqtt_dataplane.c` allocates per fragment (`malloc(event->data_len)`) and allocates per reassembly (`calloc(total_len + 1)`), then tracks up to 4 flows. That directly conflicts with MEM-01 and MEM-03: hot-path heap churn exists, and concurrent fragmented flows can increase memory pressure. The right Phase 1 shape is a single pre-allocated reassembly slot with strict offset/size validation and deterministic drops.

Use a single-flow finite-state reassembler backed by fixed storage (topic buffer + payload buffer + counters), perform bounded copy only after validating offsets and total length, and drop unsupported/invalid fragments with explicit reasons and counters. This meets MEM-01/02/03 without widening scope.

**Primary recommendation:** Replace dynamic fragment/reassembly allocation with one static pre-allocated reassembly context and a strict drop-first state machine keyed by offset progression.

## Standard Stack

### Core
| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| ESP-IDF `mqtt` component (`esp-mqtt`) | v5.5.1 docs; project on 5.5.x | Fragmented inbound MQTT via `MQTT_EVENT_DATA` | Official transport/event contract; no external MQTT lib needed |
| FreeRTOS queue/task | ESP-IDF bundled | Decouple MQTT callback from dataplane processing | Keeps callback short and predictable |
| ESP-IDF heap capabilities APIs (`esp_heap_caps`) | v5.5.1 docs | Validate memory strategy and failures (`largest_free_block`, alloc-fail hook) | Official memory model for internal/PSRAM behavior |

### Supporting
| Library | Version | Purpose | When to Use |
|---------|---------|---------|-------------|
| `esp_log` | ESP-IDF bundled | Deterministic drop reason logging | Always, for MEM-02 evidence |
| `esp_attr` (`EXT_RAM_BSS_ATTR`) | ESP-IDF bundled | Place large static buffers in external RAM where acceptable | Use for fixed reassembly storage to reduce internal RAM pressure |

### Alternatives Considered
| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| Single active reassembly flow | Multi-slot (N>1) fixed pool | Better throughput under overlap, but violates MEM-03 objective and raises RAM pressure |
| Drop-on-invalid strict sequencing | Attempt reorder/recovery logic | Improves payload recovery but adds state complexity and memory risk; conflicts with "memory stability first" |
| Static pre-allocated buffer | Dynamic `malloc/calloc/realloc` per fragment/message | Simpler short-term code, but reintroduces fragmentation/churn and fails MEM-01 |

**Installation:**
```bash
# No new packages required; use existing ESP-IDF mqtt/freertos/heap APIs
```

## Architecture Patterns

### Recommended Project Structure
```
main/connectivity/
├── mqtt_dataplane.c        # MQTT event intake + reassembly state machine
├── mqtt_dataplane.h        # public dataplane API
└── mqtt_manager.c          # client setup and event registration
```

### Pattern 1: Single-Slot Fragment Reassembly FSM
**What:** One static reassembly context with explicit states (`IDLE`, `ASSEMBLING`), expected total length, expected next offset, cached topic, and fixed payload buffer.
**When to use:** For all fragmented `MQTT_EVENT_DATA` payloads where `total_data_len > data_len`.
**Example:**
```c
// Source: https://docs.espressif.com/projects/esp-idf/en/v5.5.1/esp32s3/api-reference/protocols/mqtt.html
if (event->event_id == MQTT_EVENT_DATA) {
  // For fragmented messages, only first event contains topic/topic_len.
  // Use total_data_len/current_data_offset to assemble deterministically.
}
```

### Pattern 2: Validate-Before-Copy
**What:** Validate `offset`, `data_len`, `total_data_len`, and bounds before each `memcpy` into fixed storage.
**When to use:** Every fragment before touching reassembly buffer.
**Example:**
```c
if (frag.offset != state.next_offset) {
  drop_counter.out_of_order++;
  reset_state();
  return;
}
if (frag.total_len > CONFIG_THEO_MQTT_REASSEMBLY_MAX_BYTES) {
  drop_counter.oversize++;
  reset_state();
  return;
}
memcpy(state.payload + frag.offset, frag.data, frag.len);
state.next_offset += frag.len;
```

### Pattern 3: Deterministic Drop Taxonomy
**What:** Fixed drop reasons with counters + logs (`oversize`, `out_of_order`, `missing_topic_on_first`, `unsupported_overlap`, `queue_full`, `alloc_fail_legacy_path`).
**When to use:** Any rejected fragment or flow reset.
**Example:**
```c
ESP_LOGW(TAG, "drop reason=%s msg_id=%d off=%u len=%u total=%u",
         "out_of_order", msg_id, (unsigned)off, (unsigned)len, (unsigned)total);
```

### Anti-Patterns to Avoid
- **Per-fragment heap copies:** Causes hot-path churn and conflicts with MEM-01.
- **Using `msg_id` as sole reassembly identity:** Fragile for inbound data; strict single-flow + offset sequencing is safer for MEM-03.
- **Accepting non-zero offset while idle:** Ambiguous state; deterministically drop and count.
- **Best-effort partial publish:** Violates deterministic behavior; invalid flows should reset with explicit reason.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| MQTT transport/event parsing | Custom MQTT parser/client | ESP-IDF `esp-mqtt` event model | Protocol edge cases and maintenance burden |
| Memory telemetry primitives | Custom heap internals | `heap_caps_get_*`, `heap_caps_get_info`, alloc-fail callback | Official, capability-aware metrics |
| Queue/task synchronization primitives | Custom lock-free mailbox | FreeRTOS queue/task APIs | Proven behavior and easier debugging |

**Key insight:** Hand-roll less at protocol/memory primitive layers; hand-roll only the policy FSM (single-slot, bounded, deterministic drops).

## Common Pitfalls

### Pitfall 1: Assuming topic is present on every fragment
**What goes wrong:** Later fragments have empty topic pointer/len, causing misrouting or crashes.
**Why it happens:** ESP-MQTT only provides topic on first event for fragmented payloads.
**How to avoid:** Cache topic from first fragment (`offset==0`) and require it before completion.
**Warning signs:** `topic_len==0` with non-zero offsets; handler logic branching on raw event topic each fragment.

### Pitfall 2: Heap churn in the callback/ingest path
**What goes wrong:** Fragment bursts increase allocator pressure and fragmentation risk.
**Why it happens:** `malloc/calloc/free` per fragment/per message in hot path.
**How to avoid:** Pre-allocate fixed buffers and use bounded copy only.
**Warning signs:** Frequent alloc/free logs around `MQTT_EVENT_DATA`; largest free block trending down under load.

### Pitfall 3: Non-deterministic invalid-fragment handling
**What goes wrong:** Same malformed traffic leads to inconsistent behavior (sometimes drop, sometimes partial process).
**Why it happens:** Missing explicit drop taxonomy and state reset rules.
**How to avoid:** Define ordered validation rules and a single reset path per drop.
**Warning signs:** Ambiguous logs, missing counters, and hard-to-reproduce message handler behavior.

### Pitfall 4: Multi-flow reassembly during pressure
**What goes wrong:** Concurrent partial messages consume bounded RAM unexpectedly.
**Why it happens:** Slot-based designs with N>1 active flow in constrained systems.
**How to avoid:** Enforce exactly one active fragmented flow (MEM-03).
**Warning signs:** Flow table occupancy >1, drops only when table full, memory spikes during overlap traffic.

## Code Examples

Verified patterns from official sources and current codebase:

### ESP-MQTT fragmented event contract
```c
// Source: ESP-IDF MQTT docs
// https://docs.espressif.com/projects/esp-idf/en/v5.5.1/esp32s3/api-reference/protocols/mqtt.html
// Multiple MQTT_EVENT_DATA events may be fired for one message if it exceeds internal buffer.
// In that case, only first event contains topic/topic_len.
// Use current_data_offset and total_data_len for reassembly.
```

### Current hotspot to replace (project)
```c
// Source: main/connectivity/mqtt_dataplane.c (current implementation)
// event handler allocates per fragment:
// msg.payload.fragment.data = malloc(event->data_len);
// reassembly path allocates full payload:
// state->buffer = calloc(1, state->total_len + 1);
```

### Bounded single-flow skeleton
```c
typedef struct {
  bool active;
  size_t total_len;
  size_t next_offset;
  size_t topic_len;
  char topic[MQTT_DP_MAX_TOPIC_LEN];
  uint8_t payload[CONFIG_THEO_MQTT_REASSEMBLY_MAX_BYTES];
} mqtt_reassembly_ctx_t;

static mqtt_reassembly_ctx_t s_rx;
```

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| Per-fragment dynamic allocation and multi-flow stitching | Fixed-capacity bounded reassembly with explicit drop policy | Reliability hardening trend (2024-2026 in embedded MQTT practice) | Lower fragmentation risk; predictable failure modes |
| Best-effort parsing during malformed sequences | Deterministic reject/reset with counters | Mature production IoT reliability playbooks | Better operability and incident diagnosis |

**Deprecated/outdated:**
- Assuming fragmented MQTT events always include topic metadata: outdated; official docs define first-fragment-only topic behavior.
- Treating free-heap alone as memory safety evidence: incomplete; largest-block behavior matters for allocation success.

## Open Questions

1. **What is the required max inbound payload size for subscribed topics?**
   - What we know: Current topics are mostly short scalar strings; command payloads are small today.
   - What's unclear: Hard upper bound expected from broker integrations (future HA/entity payload changes).
   - Recommendation: Set conservative default bound now (e.g., 1-2 KiB), log oversize drops, and tune with captured traffic.

2. **Should reassembly buffer live in internal RAM or PSRAM?**
   - What we know: Project priority is to cap internal RAM pressure; code already uses `EXT_RAM_BSS_ATTR` for some structures.
   - What's unclear: Worst-case latency impact if payload buffer is in PSRAM for this path.
   - Recommendation: Keep state/control in internal RAM, put large payload buffer in PSRAM unless latency testing disproves it.

3. **How should counters be surfaced in Phase 1?**
   - What we know: MEM-02 requires explicit counters/log events; broader observability work is Phase 2.
   - What's unclear: Whether to expose counters beyond logs immediately (diagnostic topic/status API).
   - Recommendation: Implement in-memory counters + structured logs now; defer telemetry publication to Phase 2 unless trivial.

## Sources

### Primary (HIGH confidence)
- Context7 `/websites/espressif_projects_esp-idf_en_v5_5_1_esp32s3` - MQTT event fragmentation semantics, event fields, config structures.
- ESP-IDF MQTT docs (v5.5.1): https://docs.espressif.com/projects/esp-idf/en/v5.5.1/esp32s3/api-reference/protocols/mqtt.html
- ESP-IDF heap allocation docs (v5.5.1): https://docs.espressif.com/projects/esp-idf/en/v5.5.1/esp32s3/api-reference/system/mem_alloc.html
- ESP-IDF Kconfig reference (v5.5.1, MQTT options): https://docs.espressif.com/projects/esp-idf/en/v5.5.1/esp32s3/api-reference/kconfig-reference.html#config-mqtt-buffer-size

### Secondary (MEDIUM confidence)
- Espressif issue showing real-world fragmented-topic confusion (IDFGH-11179): https://github.com/espressif/esp-protocols/issues/369

### Tertiary (LOW confidence)
- Espressif issue on allocator fragmentation behavior under workload (contextual, not phase-specific proof): https://github.com/espressif/esp-idf/issues/13588

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH - Based on official ESP-IDF/Context7 APIs and existing project stack.
- Architecture: HIGH - Directly derived from MEM-01/02/03 plus verified MQTT fragment semantics.
- Pitfalls: HIGH - Confirmed by official docs + current code inspection + ecosystem issue reports.

**Research date:** 2026-02-09
**Valid until:** 2026-03-11 (30 days; stable APIs, moderate implementation churn risk)
