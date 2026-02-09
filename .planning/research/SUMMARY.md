# Project Research Summary

**Project:** ESP Theoretical Thermostat Reliability Hardening
**Domain:** Brownfield embedded thermostat firmware reliability hardening (ESP-IDF, ESP32-P4)
**Researched:** 2026-02-09
**Confidence:** HIGH

## Executive Summary

This is a reliability-hardening milestone for an existing ESP32-P4 thermostat firmware, not a greenfield product. Expert implementations in this domain do not rewrite major subsystems; they add a thin reliability control plane, harden hot memory paths, and make failures diagnosable by default. The recommended baseline is ESP-IDF 5.5.x with watchdogs enabled, core dumps enabled, stack/heap headroom telemetry always on, and deterministic bounds for MQTT fragment handling.

The strongest recommendation is to land memory safety and fault contracts before adding richer dashboards or deep debug tooling. In practice, that means replacing churn-heavy MQTT reassembly allocations with fixed-capacity bounded buffers, introducing generation-token boot fences for timeout-prone async init flows, and enforcing layered stack defenses with runtime watermark thresholds. This sequence reduces crash probability first, then improves observability quality.

The main risks are subtle heap fragmentation that free-heap-only metrics miss, false confidence from incomplete stack-overflow defenses, and race-prone timeout state handling. Mitigation is clear from the research: track largest-free-block plus minima (not only free bytes), validate stack budgets under worst-case fault/load scenarios, keep reliability state ownership centralized, and separate production-safe instrumentation from high-overhead debug profiles.

## Key Findings

### Recommended Stack

Research strongly supports staying on ESP-IDF v5.5.x patch releases and using ESP-IDF-native reliability tooling first. This gives mature watchdog behavior, stack guard options, core-dump support, and allocator observability without introducing architectural churn.

**Core technologies:**
- **ESP-IDF v5.5.1 patch line:** firmware platform and reliability primitives - best-supported bugfix line for ESP32-P4 and required hardening features.
- **IDF FreeRTOS SMP (10.5.1 lineage):** task scheduling and stack telemetry - supports per-task headroom monitoring (`uxTaskGetStackHighWaterMark2`) needed for policy thresholds.
- **`espressif/esp_insights` ^1.3.2 (+ `esp_diagnostics`):** field diagnostics pipeline - lowest-friction path for structured reliability metrics and crash context.
- **`esp-coredump` (bundled):** postmortem crash artifacts - required to avoid blind reboot loops and support actionable triage.

Critical version note: keep the firmware on ESP-IDF 5.5.x bugfix line and align diagnostics/tooling (core dump + OpenOCD/app-trace versions) with that line.

### Expected Features

The feature set is split cleanly between launch-critical reliability controls and later-stage operational enhancements. v1 should prioritize deterministic recovery behavior and RAM safety signals over analytics sophistication.

**Must have (table stakes):**
- Watchdog hardening policy (IWDT + TWDT + explicit panic behavior) for hang recovery.
- Core dump + panic/reboot contract (capture, persist, retrieve) for real postmortem debugging.
- Stack/heap runtime telemetry (stack watermarks, heap minimum, largest block, alloc-fail events).
- Deterministic fault handling contract with reboot-reason persistence.

**Should have (competitive):**
- Subsystem watchdog users for attribution (UI/MQTT/sensor/streaming starvation visibility).
- Reliability SLO diagnostics surfaced over MQTT and local diagnostics UI.
- Tiered debug profiles (prod/staging/diagnostic) to safely switch instrumentation depth.

**Defer (v2+):**
- Automated CI leak/regression gate using heap tracing baselines.
- Deep host-side tracing playbook (JTAG app_trace/SystemView) as escalation workflow.

### Architecture Approach

The architecture guidance is opinionated: keep existing subsystem ownership, add a thin `main/reliability/*` control plane, and integrate by events/snapshots rather than cross-module rewrites. This preserves brownfield stability while introducing centralized thresholds, alert policy, and health state.

**Major components:**
1. **`main/reliability/*` (new):** event IDs, health snapshot store, thresholds, alert hysteresis, and boot-fence helpers.
2. **`main/connectivity/mqtt_dataplane.c`:** bounded fixed-slot MQTT fragment reassembly with deterministic drop/oversize accounting.
3. **`main/app_main.c` + radar wrapper:** generation-token boot fence to prevent timeout/use-after-free style stale completion races.
4. **`main/connectivity/device_telemetry.c`:** publish reliability snapshots and threshold-based alerts.
5. **`main/streaming/webrtc_stream.c` and sensor modules:** emit retries/failure and online/offline reliability events into the control plane.

### Critical Pitfalls

1. **Free-heap-only monitoring blind spot** - avoid by tracking free heap, largest free block, and minimum free heap together, with ratio-based fragmentation alerts.
2. **MQTT fragment reassembly with dynamic churn** - avoid by fixed-capacity slot pools, strict payload ceilings, first-fragment topic caching, and deterministic eviction policy.
3. **Assuming canary/watchpoint catches all stack issues** - avoid by layered stack defenses plus worst-case watermark policy per critical task.
4. **Stack right-sizing from happy-path telemetry** - avoid by fault-injection and peak-load campaigns before reducing stack allocations.
5. **Debug-grade instrumentation in production** - avoid by profile separation: low-cost counters in prod, heavy tracing/poisoning only in scoped diagnostic builds.

## Implications for Roadmap

Based on combined research, use this phase structure:

### Phase 1: Memory-Safe Data Paths and Boot Race Containment
**Rationale:** Highest crash risk is internal-RAM pressure plus timeout races; this has first-order impact on uptime.
**Delivers:** Fixed-slot bounded MQTT reassembly, capability-aware allocation contracts/wrappers, and generation-token boot fence for timeout-based init flows.
**Addresses:** Table-stakes watchdog/fault contract prerequisites and core memory-stability foundation.
**Avoids:** Dynamic reassembly churn, capability-mismatched allocation, and stale async completion races.

### Phase 2: Stack Safety and Watchdog Policy Enforcement
**Rationale:** Once memory churn is bounded, enforce deterministic runtime safety envelopes around task execution.
**Delivers:** Layered stack-overflow defenses, per-task high-watermark telemetry thresholds, TWDT ownership/subscriptions for critical tasks, and validated panic behavior.
**Uses:** IDF FreeRTOS watermark APIs and ESP-IDF watchdog/fatal-error configuration.
**Implements:** Reliability events + snapshot integration for stack/watchdog status.

### Phase 3: Production Observability and Failure Handling Contract
**Rationale:** After core hardening, make failures diagnosable and operationally actionable.
**Delivers:** Heap fragmentation alerts (free/min/largest), alloc-failure callback policy (degrade vs fail-fast), reboot-reason persistence, and reliability diagnostics telemetry.
**Addresses:** Must-have telemetry/contract features and should-have SLO diagnostics.
**Avoids:** Free-heap-only blind spots and log-only alloc-failure handling.

### Phase 4: Advanced Diagnostics and Regression Prevention
**Rationale:** High-value but higher-complexity capabilities should follow a stable baseline.
**Delivers:** Tiered diagnostic profiles, subsystem watchdog attribution refinement, optional host-side tracing workflow, and CI leak/regression gating plan.
**Addresses:** Differentiators and v2-adjacent resilience engineering.
**Avoids:** Production regressions from always-on heavyweight instrumentation.

### Phase Ordering Rationale

- Dependencies favor this order: bounded data-path memory behavior and boot-fence primitives unblock trustworthy telemetry and policy work.
- Architecture grouping is clean: introduce control-plane primitives first, then instrument producers, then expose diagnostics.
- Pitfall mitigation is front-loaded: top failure modes (fragmentation churn, stack ambiguity, timeout races) are reduced before adding optional sophistication.

### Research Flags

Phases likely needing deeper research during planning:
- **Phase 4:** CI leak/regression automation and SystemView/app-trace lab workflow need team-specific harness/tooling decisions.
- **Phase 3:** Reliability SLO schema design (MQTT/Home Assistant entity shape, retention/rate limits) benefits from targeted integration research.

Phases with standard patterns (can usually skip research-phase):
- **Phase 1:** MQTT fragment semantics, fixed-buffer strategy, and boot-fence concurrency pattern are well documented.
- **Phase 2:** ESP-IDF stack/wdt configuration and watermark APIs are established and directly documented by Espressif.

## Confidence Assessment

| Area | Confidence | Notes |
|------|------------|-------|
| Stack | HIGH | Primarily grounded in official ESP-IDF and Espressif component docs for v5.5.x. |
| Features | HIGH | Prioritization aligns with official reliability guidance and strong embedded reliability consensus. |
| Architecture | HIGH | Derived from direct repository module boundaries plus documented ESP-IDF primitives. |
| Pitfalls | HIGH | Core pitfalls validated by official docs; ecosystem issue reports reinforce real-world failure patterns. |

**Overall confidence:** HIGH

### Gaps to Address

- **Telemetry schema contract detail:** finalize exact MQTT topic/entity schema, sampling cadence, and alert hysteresis during roadmap planning.
- **Threshold calibration values:** stack/heap/wdt thresholds still need hardware-specific soak/fault-test calibration on this device.
- **CI reproducibility for leak gating:** deterministic workload harness and acceptance baselines are not yet defined.
- **Core dump retention budget:** coredump partition sizing must be validated against actual task counts/stack sizes.

## Sources

### Primary (HIGH confidence)
- Context7 `/websites/espressif_projects_esp-idf_en_v5_5_1_esp32s3` - stack APIs, heap debugging, watchdogs, fatal errors, app trace, core dump.
- ESP-IDF Heap Debugging docs - https://docs.espressif.com/projects/esp-idf/en/v5.5.1/esp32p4/api-reference/system/heap_debug.html
- ESP-IDF Watchdogs docs - https://docs.espressif.com/projects/esp-idf/en/v5.5.1/esp32p4/api-reference/system/wdts.html
- ESP-IDF Fatal Errors docs - https://docs.espressif.com/projects/esp-idf/en/v5.5.1/esp32p4/api-guides/fatal-errors.html
- ESP-IDF Core Dump docs - https://docs.espressif.com/projects/esp-idf/en/v5.5.1/esp32p4/api-guides/core_dump.html
- ESP-IDF MQTT docs (`MQTT_EVENT_DATA` fragmentation semantics) - https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/protocols/mqtt.html
- ESP-IDF Memory Allocation + RAM usage docs - https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/system/mem_alloc.html, https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/performance/ram-usage.html
- Repository code boundaries examined: `main/app_main.c`, `main/connectivity/mqtt_dataplane.c`, `main/connectivity/device_telemetry.c`, `main/sensors/radar_presence.c`, `main/streaming/webrtc_stream.c`

### Secondary (MEDIUM confidence)
- ESP Insights component registry/version metadata - https://components.espressif.com/components/espressif/esp_insights
- ESP Insights docs (`esp_diagnostics` metrics/events model) - https://docs.espressif.com/projects/esp-insights/en/main/esp32/esp_insights.html
- Ecosystem issue evidence for fragmentation/fragment handling failures: https://github.com/espressif/esp-protocols/issues/369, https://github.com/espressif/esp-idf/issues/13588
- Memfault watchdog guidance (ecosystem pattern signal) - https://docs.memfault.com/docs/mcu/watchdogs

---
*Research completed: 2026-02-09*
*Ready for roadmap: yes*
