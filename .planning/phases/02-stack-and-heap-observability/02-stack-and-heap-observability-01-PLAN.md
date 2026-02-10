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
  - main/streaming/webrtc_stream.h
  - main/streaming/webrtc_stream.c
  - main/app_main.c
  - main/CMakeLists.txt
  - main/Kconfig.projbuild
  - sdkconfig.defaults
autonomous: true

must_haves:
  truths:
    - "Stack headroom bytes are sampled for mqtt dataplane, env sensors, webrtc worker, and radar-start path."
    - "Stack alert level transitions to WARN/CRIT only when configured thresholds are crossed with hysteresis and sample gating."
    - "Internal-RAM heap health snapshot includes free bytes, minimum free bytes, largest free block, and fragmentation-risk inputs."
  artifacts:
    - path: "main/connectivity/runtime_health.c"
      provides: "Periodic stack+heap sampler and threshold state machine with hysteresis"
      contains: "runtime_health_periodic_tick"
    - path: "main/connectivity/runtime_health.h"
      provides: "Public runtime-health APIs for probe registration, radar self-report, and snapshot access"
      exports: ["runtime_health_init", "runtime_health_periodic_tick", "runtime_health_record_radar_start_hwm", "runtime_health_get_snapshot"]
    - path: "main/Kconfig.projbuild"
      provides: "Configurable stack and heap warn/critical thresholds and hysteresis settings"
      contains: "CONFIG_THEO_RUNTIME_HEALTH"
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
---

<objective>
Build the runtime-health measurement and threshold engine for OBS-01/OBS-02/OBS-03/OBS-04 using existing ESP-IDF primitives and existing periodic callback wiring.

Purpose: Make stack/internal-RAM risk measurable and actionable in firmware before telemetry publishing is layered on.
Output: Runtime-health module, task probe hooks, timer/radar wiring, and configurable threshold Kconfig defaults.
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
  <name>Task 2: Add task-handle probe hooks and radar self-report path</name>
  <files>main/connectivity/mqtt_dataplane.h, main/connectivity/mqtt_dataplane.c, main/sensors/env_sensors.h, main/sensors/env_sensors.c, main/streaming/webrtc_stream.h, main/streaming/webrtc_stream.c, main/app_main.c</files>
  <action>Expose lightweight getter APIs for long-lived task handles and stack sizes from dataplane, sensor, and WebRTC modules so runtime_health can sample by handle without repeated name lookup. In `app_main.c` radar-start task path, record `uxTaskGetStackHighWaterMark2(NULL)` through runtime_health before `vTaskDelete(NULL)` to cover the short-lived radar-start requirement from research. Keep existing task ownership/lifetimes unchanged; add no new tasks.</action>
  <verify>`idf.py build` succeeds and symbol references resolve for probe getters and radar self-report API.</verify>
  <done>Runtime-health can resolve required task probes for all OBS-01 targets, including radar-start even when the task exits before periodic sampling.</done>
</task>

<task type="auto">
  <name>Task 3: Wire periodic sampling and add threshold configuration defaults</name>
  <files>main/app_main.c, main/Kconfig.projbuild, sdkconfig.defaults</files>
  <action>Initialize runtime_health during boot and invoke `runtime_health_periodic_tick()` inside existing `heap_log_timer_cb` in `app_main.c` (reuse current timer path per locked research direction, do not create a second timer). Add Kconfig options for stack warn/crit headroom bytes, heap warn/crit thresholds, fragmentation risk thresholds, hysteresis clear margins, and consecutive sample counts. Seed conservative defaults in `sdkconfig.defaults` and name all options with `CONFIG_THEO_` prefixes.</action>
  <verify>`idf.py build` succeeds and `sdkconfig` generation includes new `CONFIG_THEO_` runtime-health keys.</verify>
  <done>Runtime-health sampling runs on existing timer cadence and all threshold controls are configurable via Kconfig/defaults.</done>
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
