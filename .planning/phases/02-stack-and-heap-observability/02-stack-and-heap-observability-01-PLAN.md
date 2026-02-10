---
phase: 02-stack-and-heap-observability
plan: 01
type: execute
wave: 1
depends_on: []
files_modified:
  - main/connectivity/runtime_health.h
  - main/connectivity/runtime_health.c
  - main/connectivity/mqtt_dataplane.h
  - main/connectivity/mqtt_dataplane.c
  - main/sensors/env_sensors.h
  - main/sensors/env_sensors.c
  - main/app_main.c
  - main/CMakeLists.txt
autonomous: true

must_haves:
  truths:
    - "Operator-facing runtime-health outputs include sampled stack headroom state for mqtt dataplane, env sensors, and radar-start paths."
    - "Warning and critical stack-risk state transitions occur only after threshold crossings satisfy hysteresis and sample-gating rules."
    - "Operator-facing runtime-health outputs include internal-RAM heap free/minimum/largest and fragmentation-risk state inputs."
  artifacts:
    - path: "main/connectivity/runtime_health.c"
      provides: "Periodic stack+heap sampler and threshold state machine with hysteresis"
      contains: "runtime_health_periodic_tick"
    - path: "main/connectivity/runtime_health.h"
      provides: "Public runtime-health APIs for probe registration, radar self-report, and snapshot access"
      exports: ["runtime_health_init", "runtime_health_periodic_tick", "runtime_health_record_radar_start_hwm", "runtime_health_get_snapshot"]
  key_links:
    - from: "main/app_main.c"
      to: "main/connectivity/runtime_health.c"
      via: "heap_log_timer_cb invokes runtime health periodic tick"
      pattern: "runtime_health_periodic_tick"
    - from: "main/app_main.c"
      to: "main/connectivity/runtime_health.c"
      via: "radar_start_task self-reports stack watermark before task exit"
      pattern: "runtime_health_record_radar_start_hwm"
    - from: "main/connectivity/runtime_health.c"
      to: "main/connectivity/mqtt_dataplane.c"
      via: "task handle + configured stack depth lookup for headroom computation"
      pattern: "mqtt_dataplane_get_task_handle"
    - from: "main/connectivity/runtime_health.c"
      to: "main/sensors/env_sensors.c"
      via: "task handle + configured stack depth lookup for headroom computation"
      pattern: "env_sensors_get_task_handle"
---

<objective>
Build the runtime-health measurement and threshold engine for OBS-01/OBS-02/OBS-03/OBS-04 using existing ESP-IDF primitives and existing periodic callback wiring.

Purpose: Make stack/internal-RAM risk measurable and actionable in firmware before operator-facing observability output is layered on.
Output: Runtime-health module, task probe hooks, timer/radar wiring, and stable in-module threshold defaults.
</objective>

<execution_context>
@./.opencode/get-shit-done/workflows/execute-plan.md
@./.opencode/get-shit-done/templates/summary.md
</execution_context>

<context>
@.planning/PROJECT.md
@.planning/ROADMAP.md
@.planning/STATE.md
@.planning/phases/02-stack-and-heap-observability/02-RESEARCH.md
@.planning/phases/01-memory-safe-mqtt-reassembly/01-memory-safe-mqtt-reassembly-01-SUMMARY.md
@main/app_main.c
@main/connectivity/mqtt_dataplane.c
@main/sensors/env_sensors.c
@main/streaming/webrtc_stream.c
</context>

<tasks>

<task type="auto">
  <name>Task 1: Create runtime health sampler and threshold state machine</name>
  <files>main/connectivity/runtime_health.h, main/connectivity/runtime_health.c, main/CMakeLists.txt</files>
  <action>Create `runtime_health` module in `main/connectivity/` with no new dependencies. Implement fixed probe slots for required stack signals (`mqtt_dataplane`, `env_sensors`, `webrtc_worker`, `radar_start`) and internal-RAM heap metrics using `uxTaskGetStackHighWaterMark2` and `heap_caps_get_*` with `MALLOC_CAP_INTERNAL` only. Add a threshold engine with explicit levels (`OK/WARN/CRIT`), hysteresis clear bands, and consecutive-sample gating so alerts do not flap. Include snapshot/getter APIs consumed by telemetry later; avoid direct MQTT publish logic in this plan.</action>
  <verify>`idf.py build` succeeds with new runtime_health module compiled and linked.</verify>
  <done>Build includes `runtime_health` module, snapshots expose stack+heap+fragmentation-risk fields, and threshold transitions are deterministic with hysteresis.</done>
</task>

<task type="auto">
  <name>Task 2: Add core task-handle probe hooks and radar self-report path</name>
  <files>main/connectivity/mqtt_dataplane.h, main/connectivity/mqtt_dataplane.c, main/sensors/env_sensors.h, main/sensors/env_sensors.c, main/app_main.c</files>
  <action>Expose lightweight getter APIs for long-lived task handles and stack sizes from dataplane and sensor modules so runtime_health can sample by handle without repeated name lookup. In `app_main.c` radar-start task path, record `uxTaskGetStackHighWaterMark2(NULL)` through runtime_health before `vTaskDelete(NULL)` to cover the short-lived radar-start requirement from research. Keep existing task ownership/lifetimes unchanged; add no new tasks. Leave WebRTC probe integration to Plan 02 to keep plan scope bounded.</action>
  <verify>`idf.py build` succeeds and symbol references resolve for dataplane/sensor probe getters and radar self-report API.</verify>
  <done>Runtime-health can resolve core OBS-01 probes (dataplane, sensors, radar-start) with deterministic sampling and no new tasks.</done>
</task>

<task type="auto">
  <name>Task 3: Wire periodic sampling with in-module threshold defaults</name>
  <files>main/app_main.c, main/connectivity/runtime_health.h, main/connectivity/runtime_health.c</files>
  <action>Initialize runtime_health during boot and invoke `runtime_health_periodic_tick()` inside existing `heap_log_timer_cb` in `app_main.c` (reuse current timer path per locked research direction, do not create a second timer). Define conservative warning/critical thresholds, hysteresis clear margins, and consecutive-sample counts as compile-time constants inside runtime_health for this phase; avoid Kconfig churn in this plan.</action>
  <verify>`idf.py build` succeeds and runtime_health periodic tick executes from the existing heap timer callback with deterministic threshold transitions.</verify>
  <done>Runtime-health sampling runs on existing timer cadence with stable default threshold behavior suitable for Phase 2 execution.</done>
</task>

</tasks>

<verification>
Run `idf.py build` and confirm no compile/link regressions across enabled subsystems (MQTT, sensors, radar, optional WebRTC).
</verification>

<success_criteria>
OBS-01 instrumentation foundation exists for all required paths, OBS-02 threshold logic exists with hysteresis, and OBS-03/OBS-04 internal-RAM heap metrics are computed on periodic cadence.
</success_criteria>

<output>
After completion, create `.planning/phases/02-stack-and-heap-observability/02-stack-and-heap-observability-01-SUMMARY.md`
</output>
