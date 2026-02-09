# Project State

## Project Reference

See: `.planning/PROJECT.md` (updated 2026-02-09)

**Core value:** The thermostat must remain stable and responsive over long runtime on real hardware, with internal RAM safety treated as a first-class requirement.
**Current focus:** Phase 2 - Stack and Heap Observability

## Current Position

Phase: 2 of 3 (Stack and Heap Observability)
Plan: 0 of TBD in current phase
Status: Phase 1 complete and verified; ready to plan Phase 2
Last activity: 2026-02-09 - Verified and closed Phase 1 (Memory-Safe MQTT Reassembly)

Progress: [███░░░░░░░] 33%

## Performance Metrics

**Velocity:**
- Total plans completed: 1
- Average duration: 6 min
- Total execution time: 0.1 hours

**By Phase:**

| Phase | Plans | Total | Avg/Plan |
|-------|-------|-------|----------|
| 1. Memory-Safe MQTT Reassembly | 1 | 6 min | 6 min |
| 2. Stack and Heap Observability | 0 | 0 min | 0 min |
| 3. Fault Handling and Recovery Contract | 0 | 0 min | 0 min |

**Recent Trend:**
- Last 5 plans: 6 min
- Trend: Stable

*Updated after each plan completion*

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

### Pending Todos

None yet.

### Blockers/Concerns

None yet.

## Session Continuity

Last session: 2026-02-09 23:30
Stopped at: Phase 1 verification passed and roadmap/requirements updated
Resume file: None
