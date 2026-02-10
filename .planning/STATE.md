# Project State

## Project Reference

See: `.planning/PROJECT.md` (updated 2026-02-09)

**Core value:** The thermostat must remain stable and responsive over long runtime on real hardware, with internal RAM safety treated as a first-class requirement.
**Current focus:** Phase 2 - Stack and Heap Observability

## Current Position

Phase: 2 of 3 (Stack and Heap Observability)
Plan: 1 of 2 in current phase
Status: Phase 2 in progress; Plan 01 complete and verified
Last activity: 2026-02-10 - Completed Phase 2 Plan 01 (runtime health observability foundation)

Progress: [█████░░░░░] 50%

## Performance Metrics

**Velocity:**
- Total plans completed: 2
- Average duration: 5.5 min
- Total execution time: 0.2 hours

**By Phase:**

| Phase | Plans | Total | Avg/Plan |
|-------|-------|-------|----------|
| 1. Memory-Safe MQTT Reassembly | 1 | 6 min | 6 min |
| 2. Stack and Heap Observability | 1 | 5 min | 5 min |
| 3. Fault Handling and Recovery Contract | 0 | 0 min | 0 min |

**Recent Trend:**
- Last 5 plans: 6 min, 5 min
- Trend: Stable

*Updated after each plan completion*

| Plan Execution | Duration | Tasks | Files |
|---------------|----------|-------|-------|
| Phase 01-memory-safe-mqtt-reassembly P01 | 6 min | 3 tasks | 4 files |
| Phase 02-stack-and-heap-observability P01 | 5 min | 3 tasks | 8 files |

## Accumulated Context

### Decisions

Decisions are logged in PROJECT.md Key Decisions table.
Recent decisions affecting current work:

- [Phase 1]: Prioritize bounded fragmented MQTT reassembly and serialized flow to cap internal-RAM pressure.
- [Phase 2]: Treat stack/heap thresholds and alerting as required observability, not optional diagnostics.
- [Phase 3]: Enforce explicit watchdog and panic/core-dump contracts for unattended recovery.
- [Phase 01-memory-safe-mqtt-reassembly]: Phase 1 plan 01 hardcodes MQTT bounds at 1024-byte payload and 120-byte topic in dataplane
- [Phase 01-memory-safe-mqtt-reassembly]: Fragment policy is single active flow with freshness-first offset=0 preemption and explicit preempted counter tracking
- [Phase 01-memory-safe-mqtt-reassembly]: Dataplane digest cadence is triggered from heap_log_timer_cb via mqtt_dataplane_periodic_tick without a second timer
- [Phase 02-stack-and-heap-observability]: Use fixed pre-allocated runtime-health probe slots/snapshots to avoid per-tick allocation and protect internal RAM headroom.
- [Phase 02-stack-and-heap-observability]: Run runtime-health sampling from heap_log_timer_cb to reuse existing cadence and avoid a second timer.
- [Phase 02-stack-and-heap-observability]: Keep stack/heap WARN/CRIT thresholds as compile-time runtime_health defaults for plan-01 scope; defer Kconfig exposure.

### Pending Todos

None yet.

### Blockers/Concerns

None yet.

## Session Continuity

Last session: 2026-02-10 01:35
Stopped at: Completed 02-stack-and-heap-observability-01-PLAN.md
Resume file: None
