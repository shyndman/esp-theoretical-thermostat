# Pitfalls Research

**Domain:** ESP-IDF embedded thermostat firmware reliability hardening (memory/stack focus)
**Researched:** 2026-02-09
**Confidence:** HIGH (core guidance from ESP-IDF docs), MEDIUM (ecosystem issue patterns)

## Critical Pitfalls

### Pitfall 1: Using total free heap as the only health metric

**What goes wrong:**
The system reports "plenty of free heap" but still fails medium/large allocations under load, causing intermittent crashes or feature brownouts.

**Why it happens:**
Teams monitor `heap_caps_get_free_size()` only, and miss fragmentation (`largest_free_block` collapse).

**How to avoid:**
Track `heap_caps_get_free_size()`, `heap_caps_get_largest_free_block()`, and `heap_caps_get_minimum_free_size()` together per capability class (`MALLOC_CAP_INTERNAL`, `MALLOC_CAP_8BIT`, `MALLOC_CAP_SPIRAM` where relevant). Alert on a falling largest-block ratio, not just bytes.

**Warning signs:**
`malloc`/`heap_caps_malloc` failures despite healthy free-heap logs, rising allocation latency, and "works after reboot" behavior.

**Phase to address:**
Phase 3 - Fragmentation observability and guardrails.

---

### Pitfall 2: Reassembling MQTT fragments with churn-heavy allocation strategy

**What goes wrong:**
Large inbound MQTT payloads fragment internal RAM and occasionally crash handlers during fragmented `MQTT_EVENT_DATA` processing.

**Why it happens:**
Implementation appends chunks with repeated `malloc/realloc/free`; also assumes topic metadata exists on every fragment (it does not).

**How to avoid:**
Use bounded, reusable receive buffers/pools for MQTT payload assembly. Treat fragment state as `{msg_id, total_data_len, current_data_offset}` and cache topic from first fragment only. Enforce hard payload ceilings and reject oversize early.

**Warning signs:**
Bursty heap movement during message bursts, NULL/empty topic on later fragments, `LoadProhibited` around message handlers, and larger crashes only when payload exceeds MQTT buffer.

**Phase to address:**
Phase 1 - Internal RAM pressure reduction in MQTT fragment handling.

---

### Pitfall 3: Assuming stack canary/watchpoint catches all stack overflows

**What goes wrong:**
Real stack overflows corrupt adjacent RAM without immediate stack-overflow panic, then surface later as unrelated heap/data corruption.

**Why it happens:**
End-of-stack watchpoint and canary checks are treated as complete coverage, but ESP-IDF explicitly notes some overflows can skip these guards.

**How to avoid:**
Enable layered defenses: hardware stack guard, canary/watchpoint where useful, plus periodic high-watermark audits on stress paths. Keep per-task stack headroom policy (for example, minimum safety margin in bytes after worst-case run).

**Warning signs:**
Random corruption without stack panic, rare panic sites that move between builds, high-water marks close to zero in peak paths.

**Phase to address:**
Phase 2 - Stack safety instrumentation and policy.

---

### Pitfall 4: Right-sizing stacks from idle telemetry instead of worst-case execution

**What goes wrong:**
Task stacks look overprovisioned during nominal runs, get trimmed, then overflow in rare paths (TLS reconnect, large JSON parse, logging bursts, OTA, etc.).

**Why it happens:**
Stack measurements are collected before forcing all high-stack states; printf-heavy and error paths are not exercised.

**How to avoid:**
Profile high-water marks only after running fault-injection and peak-load scenarios. Treat internal task stack Kconfig reductions as high-risk changes; reduce incrementally and gate by repeatable stress evidence.

**Warning signs:**
Overflow or watchdog panics appear only after network failures or diagnostic logging spikes; regressions tied to "stack optimization" commits.

**Phase to address:**
Phase 2 - Stack safety instrumentation and policy.

---

### Pitfall 5: Capability-mismatched allocation (internal vs PSRAM vs DMA constraints)

**What goes wrong:**
Allocations succeed in the wrong memory region, then fail at runtime under cache-disabled windows, DMA usage, or ISR/latency-sensitive paths.

**Why it happens:**
Code defaults to generic `malloc()` and treats all free RAM as equivalent; capability flags are applied inconsistently.

**How to avoid:**
Define memory-class contracts per subsystem: control-plane and latency-critical objects in `MALLOC_CAP_INTERNAL`; DMA buffers with `MALLOC_CAP_DMA` (PSRAM excluded); large non-critical buffers explicitly in PSRAM when safe. Add static wrappers per class to prevent ad-hoc calls.

**Warning signs:**
Intermittent cache/DMA faults, memory failures only on specific transports/peripherals, large internal-RAM pressure while PSRAM remains free.

**Phase to address:**
Phase 1 - Internal RAM pressure reduction in MQTT/data-paths.

---

### Pitfall 6: Instrumentation deployed with debug-grade overhead in production path

**What goes wrong:**
Hardening itself degrades reliability: latency spikes, watchdog trips, and RAM loss from always-on heavy tracing/checking.

**Why it happens:**
Comprehensive heap poisoning, heap task tracking, or aggressive integrity checks run continuously on release firmware.

**How to avoid:**
Split instrumentation tiers: always-on low-cost counters in production; heavy tracing/poisoning behind build profile or temporary runtime switch. Timebox high-overhead modes to focused repro windows.

**Warning signs:**
Performance regressions start exactly when debug options are enabled; heap allocator throughput drops; watchdog timeouts in code that was previously stable.

**Phase to address:**
Phase 3 - Fragmentation observability and guardrails.

---

### Pitfall 7: Alloc failure handling that logs but does not isolate/fail fast

**What goes wrong:**
After first allocation failure, system continues in partially-invalid state, causing later corruption and high recovery cost.

**Why it happens:**
No registered alloc-failure callback, no subsystem degradation policy, and no clear crash-vs-degrade decision per component.

**How to avoid:**
Register failed-allocation callback globally, include requested size/caps/caller in logs, and define per-subsystem policy: drop optional work, reset submodule, or panic immediately for safety-critical paths.

**Warning signs:**
First alloc failure appears minutes before fatal crash, repeated warnings with no mitigation action, inconsistent feature behavior before reboot.

**Phase to address:**
Phase 3 - Fragmentation observability and failure policy.

---

### Pitfall 8: Timeout/offline state races across tasks (radar and telemetry paths)

**What goes wrong:**
Presence availability flaps (`online/offline`) or publishes stale transitions because timeout counters and publish decisions are updated by unsynchronized paths.

**Why it happens:**
Shared state (timeout counter, last-seen timestamp, connectivity state) is read/written from multiple contexts without single-owner serialization.

**How to avoid:**
Use one owner task/state machine for radar availability transitions. Feed it with events (read timeout, valid frame, mqtt-ready) via queue/task notification. Make publishes edge-triggered by that owner only.

**Warning signs:**
Availability toggles without matching UART evidence, duplicate transitions, behavior dependent on scheduler timing/load.

**Phase to address:**
Phase 4 - Radar timeout race fix and concurrency hardening.

---

## Technical Debt Patterns

Shortcuts that seem reasonable but create long-term problems.

| Shortcut | Immediate Benefit | Long-term Cost | When Acceptable |
|----------|-------------------|----------------|-----------------|
| One shared "scratch" buffer with hidden global ownership | Quick integration | Data races and hard-to-repro corruption | Only in single-task prototypes; never in multi-task production |
| Reducing task stacks from single happy-path run | Recovers RAM quickly | Rare-path overflows and field-only crashes | Never without worst-case stress evidence |
| Always-on comprehensive heap poisoning | Strong bug visibility | Throughput and latency penalties | Debug-only timeboxed sessions |
| Ignoring capability flags and using generic `malloc` everywhere | Less code branching | Internal RAM exhaustion and DMA/cache failures | Never in mixed-memory ESP targets |

## Integration Gotchas

Common mistakes when connecting to external services.

| Integration | Common Mistake | Correct Approach |
|-------------|----------------|------------------|
| ESP-MQTT fragmented inbound messages | Assuming topic metadata exists on every `MQTT_EVENT_DATA` chunk | Cache topic from first fragment and track message assembly by offsets/total length |
| MQTT publish API in critical tasks | Using blocking publish on long payloads inside latency-sensitive loops | Prefer enqueue/non-blocking pattern and isolate publish work in dedicated task |
| UART-based radar availability + MQTT publishing | Letting sensor read path and publish path mutate availability state independently | Single-owner availability FSM; publish transitions from that owner only |

## Performance Traps

Patterns that work at small scale but fail as usage grows.

| Trap | Symptoms | Prevention | When It Breaks |
|------|----------|------------|----------------|
| Frequent realloc during payload assembly | Heap fragmentation, jitter, occasional alloc failures | Pre-size bounded buffers or pool fixed blocks | Large retained payloads + reconnect bursts |
| Per-event deep integrity checks in hot path | Missed deadlines, watchdog events | Sampled checks plus targeted debug windows | Under network bursts / sensor spikes |
| Logging-heavy error handling inside low-stack tasks | Stack watermark collapses only during faults | Move heavy formatting to safer context or reduce log payload | During outage/reconnect storms |

## Security Mistakes

Domain-specific security issues beyond general web security.

| Mistake | Risk | Prevention |
|---------|------|------------|
| Treating alloc failures as recoverable without policy | Undefined state can expose unstable network behavior and denial-of-service loops | Define deterministic fail-fast/degrade actions per subsystem |
| Relaxing TLS/network settings to "save RAM" without threat review | MITM exposure or unauthenticated control channel risks | Keep security posture fixed; optimize memory elsewhere first |

## UX Pitfalls

Common user experience mistakes in this domain.

| Pitfall | User Impact | Better Approach |
|---------|-------------|-----------------|
| Silent feature disablement after memory pressure | Thermostat appears flaky or "stuck" | Surface degraded-mode diagnostics and recovery state via MQTT/UI telemetry |
| Radar availability flapping from race conditions | False occupancy behavior and trust erosion | Debounced, state-machine-driven availability transitions with evidence-based timeout logic |

## "Looks Done But Isn't" Checklist

Things that appear complete but are missing critical pieces.

- [ ] **MQTT fragment hardening:** Handles chunking, but no hard payload cap per topic/class.
- [ ] **Stack safety:** High-water marks collected, but no worst-case stress campaign completed.
- [ ] **Fragmentation monitoring:** Free heap is charted, but largest-free-block and low-watermark are missing.
- [ ] **Alloc failure handling:** Callback logs exist, but no subsystem-level recovery/fail-fast policy.
- [ ] **Radar timeout fix:** Counter logic changed, but ownership is still split across tasks.

## Recovery Strategies

When pitfalls occur despite prevention, how to recover.

| Pitfall | Recovery Cost | Recovery Steps |
|---------|---------------|----------------|
| Fragmentation-induced allocation failures | MEDIUM | Gate optional features, clear/recreate large transient buffers, trigger controlled subsystem restart, capture heap snapshot metrics before reboot |
| Stack overflow/corruption | HIGH | Capture panic + core dump, increase affected stack with margin, rerun worst-case stack campaign before release |
| Radar timeout race in production | MEDIUM | Disable flapping publishes, patch to single-owner FSM, validate with deterministic timeout simulation |

## Pitfall-to-Phase Mapping

How roadmap phases should address these pitfalls.

| Pitfall | Prevention Phase | Verification |
|---------|------------------|--------------|
| Free-heap-only monitoring blind spot | Phase 3 | Dashboard/telemetry includes free + largest + minimum per capability; alert fires on fragmentation ratio |
| MQTT fragment allocation churn | Phase 1 | Large-payload soak shows stable largest-block trend and no handler crashes |
| Incomplete stack-overflow detection assumptions | Phase 2 | Layered stack guards enabled; induced-overflow tests validate expected failure modes |
| Stack right-sizing from happy path only | Phase 2 | Worst-case scenario suite records stable watermark margin across all core tasks |
| Capability-mismatched allocation | Phase 1 | Allocation wrappers audited; DMA/internal/PSRAM classes verified in code review and runtime logs |
| Debug instrumentation overhead in prod | Phase 3 | Release profile excludes heavy tracing; benchmark shows no watchdog regression |
| Missing alloc-failure policy | Phase 3 | Fault injection triggers deterministic degrade/fail-fast paths with useful diagnostics |
| Radar timeout state races | Phase 4 | Deterministic timeout/frame replay yields no spurious online/offline transitions |

## Sources

- ESP-IDF Heap Memory Debugging v5.5.2 (HIGH): https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/system/heap_debug.html
- ESP-IDF Heap Memory Allocation v5.5.2 (HIGH): https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/system/mem_alloc.html
- ESP-IDF Minimizing RAM Usage v5.5.2 (HIGH): https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/performance/ram-usage.html
- ESP-IDF Fatal Errors v5.5.2 (HIGH): https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/fatal-errors.html
- ESP-MQTT docs (fragmented `MQTT_EVENT_DATA`, first event carries topic context) (HIGH): https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/protocols/mqtt.html
- espressif/esp-protocols issue #369 (fragmented topic expectations; field failure pattern) (MEDIUM): https://github.com/espressif/esp-protocols/issues/369
- espressif/esp-idf issue #13588 (real-world fragmentation pain report; allocator behavior concerns) (MEDIUM): https://github.com/espressif/esp-idf/issues/13588

---
*Pitfalls research for: embedded thermostat firmware reliability hardening*
*Researched: 2026-02-09*
