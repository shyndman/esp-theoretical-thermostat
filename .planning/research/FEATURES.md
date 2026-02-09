# Feature Research

**Domain:** Embedded firmware reliability hardening (ESP-IDF thermostat)
**Researched:** 2026-02-09
**Confidence:** HIGH

## Feature Landscape

### Table Stakes (Users Expect These)

Features users assume exist. Missing these = product feels incomplete.

| Feature | Why Expected | Complexity | Notes |
|---------|--------------|------------|-------|
| Panic capture with post-mortem analysis (`espcoredump` to flash or UART) | Production firmware must preserve crash context rather than rebooting blind | MEDIUM | Use `CONFIG_ESP_COREDUMP_TO_FLASH_OR_UART`, prefer ELF format, size coredump partition, and include `idf.py coredump-info/debug` in support workflow; depends on panic policy and partition table updates. |
| Watchdog hardening policy (IWDT + TWDT with panic for stuck tasks) | Devices in the field must self-recover from hangs and CPU starvation | MEDIUM | IWDT is default-on in ESP-IDF, but table-stakes hardening adds explicit TWDT subscriptions (`esp_task_wdt_add`/`add_user`) for critical loops and sets timeout/panic behavior intentionally; depends on per-task execution budget definitions. |
| Stack overflow defenses + stack budget telemetry | Stack overflow is a common source of silent RAM corruption in RTOS firmware | MEDIUM | Enable overflow detection (`CONFIG_FREERTOS_CHECK_STACKOVERFLOW` and/or watchpoint), collect `uxTaskGetStackHighWaterMark()` per task under worst-case load, then right-size stack configs; depends on load-test scenarios for UI, MQTT, sensors, OTA, and WebRTC concurrency. |
| Heap health observability (low-watermark, largest block, alloc-fail hooks) | Uptime issues in long-running devices often come from fragmentation and heap creep | LOW | Periodically record `heap_caps_get_minimum_free_size`, `heap_caps_get_largest_free_block`, and allocation failures (`heap_caps_register_failed_alloc_callback`); depends on adding telemetry schema and log rate limits. |
| Deterministic fault handling contract (panic mode, reboot behavior, reboot-reason persistence) | Operators expect consistent recovery behavior after fatal faults | LOW | Set `CONFIG_ESP_SYSTEM_PANIC` intentionally (not ad hoc defaults), preserve reboot reason, and document expected reboot pathways for watchdog, abort, stack, and heap failures; depends on QA test matrix for injected faults. |

### Differentiators (Competitive Advantage)

Features that set the product apart. Not required, but valuable.

| Feature | Value Proposition | Complexity | Notes |
|---------|-------------------|------------|-------|
| Reliability SLO dashboard on-device and over MQTT (heap floor, stack headroom, reset causes, watchdog incidents) | Moves from "it rebooted" to measurable reliability trends and early warning | MEDIUM | Build a compact reliability metrics payload and expose trend counters in local diagnostics UI + cloud telemetry; depends on table-stakes observability signals first. |
| Tiered debug profiles (prod/staging/diagnostic) for heap poisoning, tracing, and verbosity | Enables deep diagnosis without shipping high-overhead debug settings to all devices | MEDIUM | Keep production lean, but allow temporary field diagnostic builds with light/comprehensive heap checks and deeper traces; depends on build-profile discipline and OTA channel separation. |
| Automated leak/regression gate in CI using targeted heap tracing sessions | Catches RAM regressions before deployment instead of after long-soak field failures | HIGH | Run repeatable workload scripts and compare heap deltas + leak signatures between commits; use standalone or host-based heap tracing in controlled test runs; depends on deterministic test harness and baselines. |
| Subsystem-level watchdog users (UI loop, MQTT loop, sensor loop, media pipeline) | Pinpoints which subsystem starved rather than generic timeout logs | MEDIUM | Use `esp_task_wdt_add_user()` per critical code path and report failing user/task in crash telemetry; depends on watchdog ownership model and naming conventions. |
| Optional host-side tracing workflow (JTAG app_trace/SystemView) for deep performance forensics | Faster root cause on rare timing/race issues without instrumenting production logs heavily | HIGH | Use ESP-IDF app tracing only in lab or escalations; not a default production path; depends on reproducible reproduction cases and hardware debug access. |

### Anti-Features (Commonly Requested, Often Problematic)

Features that seem good but create problems.

| Feature | Why Requested | Why Problematic | Alternative |
|---------|---------------|-----------------|-------------|
| Always-on comprehensive heap poisoning/tracing in production | "Catch every bug in the field" | ESP-IDF documents substantial runtime and RAM overhead; can itself reduce stability and battery/thermal margins | Keep production on low-overhead checks + telemetry; enable heavy diagnostics only in controlled debug profile or canary cohort |
| Reboot-on-error with no persistent crash artifact | "Fast recovery, minimal storage use" | Destroys root-cause evidence and creates reboot loops that are impossible to diagnose remotely | Store core dump or at least compact crash signature + reboot reason before restart |
| Turning off watchdogs for "stability" during long operations | "Avoid false positives while doing heavy work" | Converts transient stalls into permanent hangs and destroys uptime guarantees | Tune timeouts and feed strategy around known long operations; instrument long critical sections instead of disabling watchdogs |
| Unbounded reliability logging over MQTT/UART | "More logs means easier debugging" | Can create self-inflicted CPU/network pressure and timing jitter, masking root cause | Use rate-limited counters, sampling windows, and event-based escalation logs |

## Feature Dependencies

```text
[Panic policy + reboot contract]
    └──requires──> [Core dump enablement + partitioning]
                         └──requires──> [Crash retrieval & symbolication workflow]

[Task stack telemetry]
    └──requires──> [Stack overflow detection configuration]
                         └──requires──> [Worst-case workload test scenarios]

[Reliability SLO dashboard]
    └──requires──> [Heap + stack + watchdog signal collection]
                         └──requires──> [Stable telemetry schema]

[Subsystem watchdog users] ──enhances──> [Watchdog hardening policy]

[Always-on comprehensive tracing] ──conflicts──> [Production performance/uptime budget]
```

### Dependency Notes

- **Panic policy + reboot contract requires core dump enablement:** the panic path determines whether useful crash state exists after reboot.
- **Task stack telemetry requires stack overflow detection configuration:** watermarks without overflow guards can miss catastrophic overruns.
- **Reliability SLO dashboard requires stable telemetry schema:** trends are only useful if metrics are consistently named and versioned.
- **Subsystem watchdog users enhance watchdog policy:** per-user resets localize starvation to a component rather than "system hung".
- **Always-on comprehensive tracing conflicts with uptime budget:** high-overhead diagnostics should be scoped, not default.

## MVP Definition

### Launch With (v1)

Minimum viable product for this milestone.

- [ ] Watchdog hardening policy (IWDT/TWDT config + panic behavior + per-critical-task coverage) — baseline self-recovery and hang detection.
- [ ] Core dump + panic handling contract (capture, retain, retrieve) — baseline post-mortem diagnosability.
- [ ] Stack and heap runtime telemetry (high-watermarks, low-watermarks, fragmentation indicator, alloc-fail events) — baseline RAM safety observability.

### Add After Validation (v1.x)

Features to add once core is working.

- [ ] Subsystem watchdog users with richer timeout attribution — add when base TWDT tuning is stable.
- [ ] Reliability SLO dashboard over MQTT/UI — add when telemetry schema is stable across at least one soak cycle.

### Future Consideration (v2+)

Features to defer until product reliability baseline is proven.

- [ ] Automated leak/regression CI gate with heap tracing baselines — defer until deterministic load harness exists.
- [ ] Deep host-side tracing playbook (SystemView/app_trace escalation path) — defer until team has repeatable lab workflow.

## Feature Prioritization Matrix

| Feature | User Value | Implementation Cost | Priority |
|---------|------------|---------------------|----------|
| Watchdog hardening policy | HIGH | MEDIUM | P1 |
| Core dump + panic contract | HIGH | MEDIUM | P1 |
| Stack/heap runtime telemetry | HIGH | LOW | P1 |
| Subsystem watchdog users | MEDIUM | MEDIUM | P2 |
| Reliability SLO dashboard | MEDIUM | MEDIUM | P2 |
| CI leak/regression gate | MEDIUM | HIGH | P3 |
| Host-side tracing workflow | LOW | HIGH | P3 |

**Priority key:**
- P1: Must have for launch
- P2: Should have, add when possible
- P3: Nice to have, future consideration

## Competitor Feature Analysis

| Feature | Competitor A | Competitor B | Our Approach |
|---------|--------------|--------------|--------------|
| Crash diagnostics | Many ESP-IDF projects stop at UART panic logs only | Fleet-observability stacks persist coredumps and symbolicate centrally | Keep ESP-IDF-native coredump as baseline, then add lightweight crash signatures to MQTT telemetry |
| Watchdog usage | Commonly relies on default config with minimal task attribution | Mature products map watchdogs to explicit subsystem heartbeats | Start with strict TWDT/IWDT policy, then add per-subsystem `add_user` attribution |
| RAM observability | Basic free-heap logging, little fragmentation/stack trend tracking | Reliability-focused products trend headroom and resets over time | Track heap floor + largest block + stack headroom and trend across uptime windows |

## Sources

- HIGH: ESP-IDF Heap Memory Debugging (v5.5.2), including heap corruption detection, task tracking, tracing, and alloc hooks: https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/system/heap_debug.html
- HIGH: ESP-IDF Watchdogs (v5.5.2), including IWDT/TWDT behavior and configuration: https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/system/wdts.html
- HIGH: ESP-IDF Core Dump guide (v5.5.2): https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/core_dump.html
- HIGH: ESP-IDF Fatal Errors and panic handler behavior (v5.5.2): https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/fatal-errors.html
- HIGH: ESP-IDF Minimizing RAM Usage, including stack sizing and overflow detection guidance (v5.5.2): https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/performance/ram-usage.html
- MEDIUM: ESP-IDF Application Level Tracing for host-side diagnostics (v5.5.2): https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/system/app_trace.html
- MEDIUM: Memfault watchdog instrumentation guidance (used as ecosystem signal for fleet-grade practices, not as mandatory stack): https://docs.memfault.com/docs/mcu/watchdogs

---
*Feature research for: ESP-IDF thermostat reliability hardening milestone*
*Researched: 2026-02-09*
