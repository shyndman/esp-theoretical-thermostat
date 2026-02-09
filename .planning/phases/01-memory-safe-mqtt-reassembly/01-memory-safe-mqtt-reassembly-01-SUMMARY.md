---
phase: 01-memory-safe-mqtt-reassembly
plan: 01
subsystem: api
tags: [mqtt, esp-idf, psram, reliability, logging]

# Dependency graph
requires:
  - phase: none
    provides: initial roadmap and memory-safety requirements
provides:
  - single-slot bounded MQTT fragment reassembly with freshness-first preemption
  - deterministic drop taxonomy counters and minute digest logging contract
  - heap-timer piggyback digest scheduling and manual verification matrix
affects: [stack-observability, heap-observability, fault-recovery]

# Tech tracking
tech-stack:
  added: []
  patterns: [single-slot reassembly FSM, one-reason drop accounting, timer-piggyback periodic digest]

key-files:
  created: [.planning/phases/01-memory-safe-mqtt-reassembly/01-memory-safe-mqtt-reassembly-01-SUMMARY.md]
  modified: [main/connectivity/mqtt_dataplane.c, main/connectivity/mqtt_dataplane.h, main/app_main.c, docs/manual-test-plan.md]

key-decisions:
  - "Hardcode MQTT reassembly bounds at 1024-byte payload and 120-byte topic in dataplane source."
  - "Use one active flow with valid offset=0 newcomer preemption and explicit preempted counter visibility."
  - "Emit minute digest from heap timer callback via dataplane tick API instead of introducing a second periodic timer."

patterns-established:
  - "Bounded Reassembly: keep payload storage pre-allocated in PSRAM and avoid hot-path fragment allocations."
  - "Digest Contract: log cumulative plus delta counters every 60s even when interval deltas are zero."

# Metrics
duration: 6 min
completed: 2026-02-09
---

# Phase 1 Plan 1: Memory-Safe MQTT Reassembly Summary

**Single-slot MQTT reassembly now runs with a 1024-byte hard cap, deterministic drop taxonomy, and minute digest visibility piggybacked on the existing heap monitor timer.**

## Performance

- **Duration:** 6 min
- **Started:** 2026-02-09T23:19:54Z
- **Completed:** 2026-02-09T23:26:24Z
- **Tasks:** 3
- **Files modified:** 4

## Accomplishments
- Replaced multi-slot dynamic fragment handling with one active reassembly context and PSRAM payload storage bounded at 1024 bytes.
- Enforced fixed one-reason drop taxonomy (`oversize`, `out_of_order`, `nonzero_first`, `overlap`, `queue_full`) with freshness-first offset=0 preemption and preempt counter tracking.
- Added externally triggered 60-second digest cadence with always-on heartbeat lines including total and delta fields for required counters.
- Wired digest ticking to `heap_log_timer_cb` and documented manual verification cases for valid flow, all drop reasons, queue pressure, and zero-delta heartbeat behavior.

## Task Commits

Each task was committed atomically:

1. **Task 1: Implement locked single-slot reassembly policy and hardcoded bounds** - `8a62b33` (feat)
2. **Task 2: Add periodic digest accounting contract on dataplane side** - `a409b36` (feat)
3. **Task 3: Piggyback digest emission on heap timer and document verification matrix** - `79a680b` (docs)

## Files Created/Modified
- `main/connectivity/mqtt_dataplane.c` - Single-slot reassembly FSM, drop accounting, and periodic digest emission logic.
- `main/connectivity/mqtt_dataplane.h` - Public periodic tick API declaration used by app timer callback.
- `main/app_main.c` - Heap monitor timer callback now triggers dataplane periodic digest tick.
- `docs/manual-test-plan.md` - Phase 1 verification matrix for all mandatory drop reasons and digest heartbeat expectations.
- `.planning/phases/01-memory-safe-mqtt-reassembly/01-memory-safe-mqtt-reassembly-01-SUMMARY.md` - Plan execution summary.

## Decisions Made
- Hardcoded payload/topic bounds in dataplane compile-time constants and kept them out of Kconfig for Phase 1 lock compliance.
- Implemented strict single-slot flow ownership where a valid newcomer at offset 0 preempts the active flow immediately.
- Kept digest scheduling external by exposing `mqtt_dataplane_periodic_tick(now_us)` and invoking it from the existing heap timer.

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered

None.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

Phase 1 Plan 01 complete and verified with `idf.py build`; Phase 2 can build on digest counters for stack/heap observability thresholds and alerting.

---
*Phase: 01-memory-safe-mqtt-reassembly*
*Completed: 2026-02-09*

## Self-Check: PASSED

- Found `.planning/phases/01-memory-safe-mqtt-reassembly/01-memory-safe-mqtt-reassembly-01-SUMMARY.md`.
- Verified task commits `8a62b33`, `a409b36`, and `79a680b` exist in repository history.
