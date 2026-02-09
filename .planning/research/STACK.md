# Stack Research

**Domain:** Embedded thermostat firmware reliability hardening (ESP-IDF, memory/stack/observability)
**Researched:** 2026-02-09
**Confidence:** HIGH

## Recommended Stack

### Core Technologies

| Technology | Version | Purpose | Why Recommended |
|------------|---------|---------|-----------------|
| ESP-IDF | v5.5.1 patch line | Primary firmware framework | Current Espressif bugfix line for ESP32-P4; includes mature heap debugging, watchdog, core dump, app trace, and hardware stack guard support needed for long-uptime hardening. |
| IDF FreeRTOS (SMP) | bundled with ESP-IDF v5.5.1 (based on FreeRTOS v10.5.1) | Tasking, stack sizing, and scheduling | Exposes `uxTaskGetStackHighWaterMark2()` and compatible task APIs for per-task headroom monitoring without architecture rewrite. |
| ESP Insights agent (`espressif/esp_insights`) | ^1.3.2 | Fleet/runtime diagnostics channel | Standard Espressif path for field diagnostics (logs, metrics, reset/crash context) with low integration friction through Component Manager. |

### Supporting Libraries

| Library | Version | Purpose | When to Use |
|---------|---------|---------|-------------|
| `espressif/esp_insights` | ^1.3.2 | Device telemetry upload (error/warn/event logs, metrics, crash context) | Use for production observability where LAN-only devices still need postmortem visibility across reboots and unattended runtime. |
| `esp_diagnostics` (pulled by Insights) | bundled via `esp_insights` | Structured diagnostics events and task snapshots | Use to emit reliability metrics (heap free, largest block, stack headroom thresholds, watchdog trigger context) in machine-readable form. |
| `esp-coredump` component | bundled with ESP-IDF | Crash snapshot capture to flash/UART + host decoding | Enable for every reliability hardening milestone so stack/TCB state is recoverable after field failures. |

### Development Tools

| Tool | Purpose | Notes |
|------|---------|-------|
| `idf.py monitor`, `idf.py coredump-info`, `idf.py coredump-debug` | Fast crash triage and postmortem decoding | Treat as mandatory in CI/manual validation for every panic, watchdog timeout, or stack fault. |
| OpenOCD + App Trace/SystemView | Heap tracing and scheduler behavior tracing over JTAG | Use for targeted debug sessions (leaks, starvation, unexpected long critical sections); not always-on in production. |
| `sdkconfig.defaults` reliability profile | Reproducible reliability config baseline | Keep hardening toggles explicit and versioned (watchdogs, stack checks, core dump, panic mode, heap debug modes). |

## Installation

```bash
# Keep framework on current bugfix line
git submodule update --init --recursive

# Add field diagnostics agent
idf.py add-dependency "espressif/esp_insights^1.3.2"

# Reconfigure with reliability options
idf.py menuconfig
```

## Alternatives Considered

| Recommended | Alternative | When to Use Alternative |
|-------------|-------------|-------------------------|
| ESP Insights + esp_diagnostics | Memfault firmware SDK | Use Memfault only if your org already standardizes on Memfault backend and has validated support for your exact ESP-IDF line and targets. |
| Core dump to flash (ELF format) + `idf.py coredump-*` | UART-only core dump | Use UART-only for bring-up boards without writable coredump partition, but flash destination is better for unattended reboot scenarios. |
| Runtime heap fragmentation KPIs from `heap_caps_get_info()` and `heap_caps_get_largest_free_block()` | Ad-hoc free-heap-only logging | Use free-heap-only logging only for tiny prototypes; it misses fragmentation and fails to predict allocation failures. |

## What NOT to Use

| Avoid | Why | Use Instead |
|-------|-----|-------------|
| Long-lived dynamic MQTT reassembly buffers and variable-size message stitching | Drives heap fragmentation and late-runtime allocation failures under sustained traffic | Fixed-capacity serialized reassembly buffers with bounded message size and explicit overflow handling. |
| `CONFIG_HEAP_TASK_TRACKING` as always-on production telemetry | Official docs warn of non-negligible RAM overhead and severe allocator performance impact | Use it only in focused debug builds; keep production on lightweight periodic heap KPIs. |
| Heap corruption detection `Comprehensive` mode in production | Detects subtle issues but has substantial runtime overhead by design | Use `Light Impact` in debug validation, then disable/relax for production once validated. |
| Disabling IWDT/TWDT to suppress resets | Hides deadlocks/starvation and defers failures to undefined behavior | Keep watchdogs enabled, tune timeouts, and instrument offending task/user paths. |

## Stack Patterns by Variant

**If production firmware (long uptime focus):**
- Use IWDT + TWDT enabled, core dump to flash (ELF), hardware stack guard enabled.
- Use periodic metrics: internal free heap, largest free block, min-ever free heap, and per-task stack high-watermark.

**If deep debug firmware (short test windows):**
- Enable heap tracing, heap poisoning (`Light Impact` or temporarily `Comprehensive`), stack watchpoint, and richer app trace/SystemView events.
- Keep this profile separate from production to avoid false performance/regression conclusions.

## Version Compatibility

| Package A | Compatible With | Notes |
|-----------|-----------------|-------|
| `esp-idf v5.5.1` | `espressif/esp_insights ^1.3.2` | Component registry lists current `esp_insights` release and IDF-managed integration path. |
| Core dump (ELF) | `idf.py coredump-info` / `idf.py coredump-debug` | Recommended format for richer postmortem data; ensure coredump partition sizing matches task count/stack sizes. |
| App Trace + SystemView | OpenOCD `v0.12.0-esp32-20250707` or newer | v5.5.1 release notes document updated OpenOCD/toolchain path for trace workflows. |

## Prescriptive 2025 Hardening Baseline (apply first)

1. Pin to ESP-IDF `v5.5.x` bugfix releases, not mixed custom forks.
2. Replace dynamic protocol reassembly with fixed-buffer serialization and strict bounds checks.
3. Ship with watchdogs enabled (IWDT + TWDT), panic path configured for actionable output, and core dump enabled.
4. Add continuous stack headroom monitoring from `uxTaskGetStackHighWaterMark2()` with per-task thresholds and WARN/ERROR escalation.
5. Add fragmentation observability using `largest_free_block / total_free_heap` and min-ever free heap, not free heap alone.
6. Keep heavyweight heap task tracking and comprehensive poisoning in debug-only builds.

## Sources

- Context7 `/websites/espressif_projects_esp-idf_en_v5_5_1_esp32s3` - heap debugging, stack APIs, watchdogs, app trace, core dump (`HIGH`)
- https://docs.espressif.com/projects/esp-idf/en/v5.5.1/esp32p4/api-reference/system/heap_debug.html - fragmentation and heap debugging guidance (`HIGH`)
- https://docs.espressif.com/projects/esp-idf/en/v5.5.1/esp32p4/api-reference/system/wdts.html - IWDT/TWDT behavior and tuning (`HIGH`)
- https://docs.espressif.com/projects/esp-idf/en/v5.5.1/esp32p4/api-guides/fatal-errors.html - stack guard/watchpoint/stack-smashing and panic behavior (`HIGH`)
- https://docs.espressif.com/projects/esp-idf/en/v5.5.1/esp32p4/api-guides/core_dump.html - coredump destination/format and analysis tooling (`HIGH`)
- https://docs.espressif.com/projects/esp-idf/en/v5.5.1/esp32p4/api-guides/app_trace.html - tracing/SystemView and host tooling (`HIGH`)
- https://github.com/espressif/esp-idf/releases/tag/v5.5.1 - release maturity and toolchain/openocd updates (`HIGH`)
- https://components.espressif.com/components/espressif/esp_insights - current registry version and integration command (`MEDIUM`, registry page)
- https://docs.espressif.com/projects/esp-insights/en/main/esp32/esp_insights.html - Insights APIs and transport model (`HIGH`)
- https://docs.espressif.com/projects/esp-insights/en/main/esp32/esp_diag_metrics.html - system metrics including heap polling support (`HIGH`)

---
*Stack research for: ESP Theoretical Thermostat Reliability Hardening*
*Researched: 2026-02-09*
