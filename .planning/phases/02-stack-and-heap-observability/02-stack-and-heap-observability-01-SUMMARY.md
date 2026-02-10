---
phase: 02-stack-and-heap-observability
plan: 01
subsystem: infra
tags: [esp-idf, freertos, stack-monitoring, heap-observability, runtime-health]

# Dependency graph
requires:
  - phase: 01-memory-safe-mqtt-reassembly
    provides: bounded dataplane periodic tick hook reused for runtime health cadence
provides:
  - runtime health module with fixed stack probe slots and internal-RAM heap snapshots
  - deterministic WARN/CRIT threshold transitions using hysteresis and consecutive-sample gates
  - boot and timer wiring for dataplane, env sensors, and radar-start observability foundations
affects: [02-stack-and-heap-observability-02, fault-recovery]

# Tech tracking
tech-stack:
  added: []
  patterns: [fixed probe-slot observability state, timer-piggyback health sampling, hysteresis threshold state machine]

key-files:
  created:
    - .planning/phases/02-stack-and-heap-observability/02-stack-and-heap-observability-01-SUMMARY.md
    - main/connectivity/runtime_health.h
    - main/connectivity/runtime_health.c
  modified:
    - main/app_main.c
    - main/CMakeLists.txt
    - main/connectivity/mqtt_dataplane.h
    - main/connectivity/mqtt_dataplane.c
    - main/sensors/env_sensors.h
    - main/sensors/env_sensors.c

key-decisions:
  - "Use fixed, pre-allocated probe slots and snapshot structs to avoid per-tick dynamic allocation in runtime health sampling."
  - "Keep runtime-health cadence on heap_log_timer_cb and avoid introducing a second periodic timer."
  - "Configure phase-1 thresholds as in-module compile-time defaults and defer Kconfig surfacing to a later scope."

patterns-established:
  - "Probe Ownership: long-lived tasks expose handle/stack getter APIs for external observability modules."
  - "Ephemeral Coverage: short-lived radar-start path self-reports stack watermark before task deletion."

# Metrics
duration: 5 min
completed: 2026-02-10
---

# Phase 2 Plan 1: Stack and Heap Observability Summary

**Runtime health now samples core task stack headroom and internal-RAM heap fragmentation signals on the existing heap timer cadence with deterministic WARN/CRIT hysteresis transitions.**

## Performance

- **Duration:** 5 min
- **Started:** 2026-02-10T01:29:25Z
- **Completed:** 2026-02-10T01:35:02Z
- **Tasks:** 3
- **Files modified:** 8

## Accomplishments

- Added `runtime_health` foundation module with fixed probe slots for `mqtt_dataplane`, `env_sensors`, `webrtc_worker`, and `radar_start`, plus internal-RAM heap metrics (`free`, `minimum free`, `largest block`, ratio).
- Implemented threshold policy engine with explicit `OK/WARN/CRIT`, hysteresis clear bands, and consecutive-sample gating for deterministic non-flapping transitions.
- Exposed task-handle/stack-size getters in dataplane and sensor modules, and added radar-start self-report path before `vTaskDelete(NULL)`.
- Wired boot initialization and periodic runtime health sampling into the existing `heap_log_timer_cb` path without creating extra timers or tasks.

## Task Commits

Each task was committed atomically:

1. **Task 1: Create runtime health sampler and threshold state machine** - `bfc4b75` (feat)
2. **Task 2: Add core task-handle probe hooks and radar self-report path** - `624bf7f` (feat)
3. **Task 3: Wire periodic sampling with in-module threshold defaults** - `3f902f2` (feat)

## Files Created/Modified

- `main/connectivity/runtime_health.h` - Public runtime-health probe IDs, snapshots, and API surface.
- `main/connectivity/runtime_health.c` - Stack/heap sampler, threshold state machine, and probe configuration logic.
- `main/connectivity/mqtt_dataplane.h` - Dataplane task handle and stack-size getter declarations.
- `main/connectivity/mqtt_dataplane.c` - Dataplane getter implementations.
- `main/sensors/env_sensors.h` - Environmental sensor task handle and stack-size getter declarations.
- `main/sensors/env_sensors.c` - Environmental sensor getter implementations.
- `main/app_main.c` - Runtime health initialization, periodic tick wiring, and radar-start self-report integration.
- `main/CMakeLists.txt` - Runtime health module added to firmware build.

## Decisions Made

- Fixed probe storage and snapshots are static/pre-allocated to preserve internal-RAM headroom and avoid per-tick allocation churn.
- Runtime health is initialized before heap monitor startup so the first periodic callback samples deterministic state.
- WebRTC probe slot is reserved in this plan but left unconfigured for Plan 02 integration scope control.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] STATE.md automation helpers could not parse legacy state format**
- **Found during:** Post-task state update stage
- **Issue:** `gsd-tools state advance-plan`, `state update-progress`, and `state record-session` returned parse errors against existing STATE.md structure.
- **Fix:** Updated `.planning/STATE.md` manually to reflect current plan position, progress, metrics, and session continuity.
- **Files modified:** `.planning/STATE.md`
- **Verification:** Re-read STATE.md and confirmed Phase 2/Plan 1 status, metrics, and new decisions are present.
- **Committed in:** `60db9ee`

---

**Total deviations:** 1 auto-fixed (1 blocking)
**Impact on plan:** Deviation only affected planning metadata automation; firmware scope and deliverables remained within OBS-01..OBS-04 foundation.

## Issues Encountered

- `gsd-tools` state automation commands could not parse the repository's current STATE.md schema, so state updates were applied manually.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

Phase 2 Plan 01 is complete and verified by `idf.py build`; Plan 02 can layer telemetry publishing and HA discovery over the runtime-health snapshot/level APIs.

---
*Phase: 02-stack-and-heap-observability*
*Completed: 2026-02-10*

## Self-Check: PASSED

- Found `.planning/phases/02-stack-and-heap-observability/02-stack-and-heap-observability-01-SUMMARY.md`.
- Verified task commits `bfc4b75`, `624bf7f`, and `3f902f2` exist in repository history.
