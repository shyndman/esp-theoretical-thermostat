# Project State

## Project Reference

See: `.planning/PROJECT.md` (updated 2026-02-09)

**Core value:** The thermostat must remain stable and responsive over long runtime on real hardware, with internal RAM safety treated as a first-class requirement.
**Current focus:** Phase 2.1 (INSERTED) - RAM Attribution and Reduction

## Current Position

Phase: 2.1 of 4 (RAM Attribution and Reduction)
Plan: 1 of 4 in current phase
Status: Phase 2.1 underway; wave-1 RAM attribution + default-off WebRTC gate implemented and ready for acceptance checkpoint
Last activity: 2026-02-10 - Completed Phase 2.1 plan 01 (RAM attribution + wave-1 gate)

Progress: [██████░░░░] 57%

## Performance Metrics

**Velocity:**
- Total plans completed: 4
- Average duration: 5.3 min
- Total execution time: 0.4 hours

**By Phase:**

| Phase | Plans | Total | Avg/Plan |
|-------|-------|-------|----------|
| 1. Memory-Safe MQTT Reassembly | 1 | 6 min | 6 min |
| 2. Stack and Heap Observability | 2 | 9 min | 4.5 min |
| 2.1. RAM Attribution and Reduction (INSERTED) | 1 | 6 min | 6 min |
| 3. Fault Handling and Recovery Contract (Parked) | 0 | 0 min | 0 min |

**Recent Trend:**
- Last 5 plans: 6 min, 5 min, 4 min, 6 min
- Trend: Stable

*Updated after each plan completion*

| Plan Execution | Duration | Tasks | Files |
|---------------|----------|-------|-------|
| Phase 01-memory-safe-mqtt-reassembly P01 | 6 min | 3 tasks | 4 files |
| Phase 02-stack-and-heap-observability P01 | 5 min | 3 tasks | 8 files |
| Phase 02-stack-and-heap-observability P02 | 4 min | 3 tasks | 4 files |
| Phase 02.1-memory-usage-reduction-as-discussed P01 | 6 min | 3 tasks | 10 files |

## Accumulated Context

### Decisions

Decisions are logged in PROJECT.md Key Decisions table.
Recent decisions affecting current work:

- [Phase 1]: Prioritize bounded fragmented MQTT reassembly and serialized flow to cap internal-RAM pressure.
- [Phase 2]: Treat stack/heap thresholds and alerting as required observability, not optional diagnostics.
- [Phase 2.1]: Insert urgent RAM attribution/reduction phase and keep all tuning changes config-gated (default off) until validated in combination.
- [Phase 3]: Parked pending Phase 2.1 completion due to unresolved internal-RAM pressure and fragmentation risk.
- [Phase 3]: Enforce explicit watchdog and panic/core-dump contracts for unattended recovery.
- [Phase 01-memory-safe-mqtt-reassembly]: Phase 1 plan 01 hardcodes MQTT bounds at 1024-byte payload and 120-byte topic in dataplane
- [Phase 01-memory-safe-mqtt-reassembly]: Fragment policy is single active flow with freshness-first offset=0 preemption and explicit preempted counter tracking
- [Phase 01-memory-safe-mqtt-reassembly]: Dataplane digest cadence is triggered from heap_log_timer_cb via mqtt_dataplane_periodic_tick without a second timer
- [Phase 02-stack-and-heap-observability]: Use fixed pre-allocated runtime-health probe slots/snapshots to avoid per-tick allocation and protect internal RAM headroom.
- [Phase 02-stack-and-heap-observability]: Run runtime-health sampling from heap_log_timer_cb to reuse existing cadence and avoid a second timer.
- [Phase 02-stack-and-heap-observability]: Keep stack/heap WARN/CRIT thresholds as compile-time runtime_health defaults for plan-01 scope; defer Kconfig exposure.
- [Phase 02-stack-and-heap-observability]: Keep Phase 2 observability output local-only in structured serial logs (no MQTT/HA publication).
- [Phase 02-stack-and-heap-observability]: Gate periodic runtime-health logs with fixed wall-clock cadence using pre-allocated state only.
- [Phase 02-stack-and-heap-observability]: Emit explicit stack/heap level transition logs while preserving hysteresis and consecutive-sample gating.
- [Phase 02.1-memory-usage-reduction-as-discussed]: Keep RAM attribution on existing runtime_health periodic cadence with no second timer and no per-tick allocation.
- [Phase 02.1-memory-usage-reduction-as-discussed]: Keep WebRTC wave-1 memory tuning behind explicit default-off gate `CONFIG_THEO_RAM_WAVE1_WEBRTC_TUNING`.
- [Phase 02.1-memory-usage-reduction-as-discussed]: Attribute dynamic allocation churn via webrtc_stream/whep_endpoint counters and emit periodic deltas in `runtime_health_ram_attr` logs.

### Roadmap Evolution

- Phase 02.1 inserted after Phase 2: memory usage reduction, as discussed (URGENT)

### Pending Todos

None yet.

### Blockers/Concerns

None yet.

## Session Continuity

Last session: 2026-02-10 16:50
Stopped at: Completed 02.1-memory-usage-reduction-as-discussed-01-PLAN.md
Resume file: None
