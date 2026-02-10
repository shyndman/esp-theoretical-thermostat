# Requirements: ESP Theoretical Thermostat Reliability Hardening

**Defined:** 2026-02-09
**Core Value:** The thermostat must remain stable and responsive over long runtime on real hardware, with internal RAM safety treated as a first-class requirement.

## v1 Requirements

### Memory Safety

- [x] **MEM-01**: Firmware can reassemble fragmented MQTT payloads using bounded pre-allocated capacity without per-fragment hot-path heap allocation.
- [x] **MEM-02**: Firmware can deterministically drop oversize, out-of-order, or unsupported fragmented MQTT payloads with explicit counters/log events.
- [x] **MEM-03**: Firmware can enforce a single active fragmented-message reassembly flow to cap concurrent internal-RAM pressure.

### Stack & Heap Observability

- [x] **OBS-01**: Firmware can record and expose per-task stack high-watermark metrics for critical tasks (MQTT dataplane, sensors, WebRTC worker, and radar-start path).
- [x] **OBS-02**: Firmware can evaluate stack safety thresholds and emit warning/critical alerts when configured headroom limits are crossed.
- [x] **OBS-03**: Firmware can publish heap health metrics including free bytes, minimum free bytes, and largest free block in internal RAM.
- [x] **OBS-04**: Firmware can compute and surface heap-fragmentation risk from largest-block trends with threshold-based alerts.

### Fault Handling & Runtime Safety

- [ ] **SAFE-01**: Firmware can complete radar start timeout handling without stale task/context lifetime violations.
- [ ] **SAFE-02**: Firmware can run with explicit watchdog policy (IWDT/TWDT settings and critical-task coverage) suitable for unattended recovery.
- [ ] **SAFE-03**: Firmware can preserve deterministic fault evidence and reboot behavior via panic/core-dump handling contract.

## v2 Requirements

### Advanced Reliability Operations

- **RELOPS-01**: Firmware can attribute watchdog incidents to subsystem-level watchdog users.
- **RELOPS-02**: Firmware can expose reliability SLO diagnostics in local UI and MQTT telemetry views.
- **RELOPS-03**: Firmware can support tiered diagnostics profiles (prod/staging/diagnostic) for instrumentation depth control.
- **RELOPS-04**: CI can run leak/regression detection gates using repeatable heap-tracing baselines.
- **RELOPS-05**: Team can run an escalation playbook for host-side deep tracing (app_trace/SystemView).

## Out of Scope

| Feature | Reason |
|---------|--------|
| Hardware portability refactors for alternate boards/display geometry | Current milestone is for a single fixed thermostat unit |
| MQTT command-topic authentication/signing | Explicitly deprioritized for current LAN-trusted deployment |
| LVGL lock-contention optimization | Deferred unless profiling shows user-visible issues |
| MQTT topic-length scaling changes | Explicitly deprioritized by owner for this milestone |

## Traceability

| Requirement | Phase | Status |
|-------------|-------|--------|
| MEM-01 | Phase 1 | Complete |
| MEM-02 | Phase 1 | Complete |
| MEM-03 | Phase 1 | Complete |
| OBS-01 | Phase 2 | Complete |
| OBS-02 | Phase 2 | Complete |
| OBS-03 | Phase 2 | Complete |
| OBS-04 | Phase 2 | Complete |
| SAFE-01 | Phase 3 | Pending |
| SAFE-02 | Phase 3 | Pending |
| SAFE-03 | Phase 3 | Pending |

**Coverage:**
- v1 requirements: 10 total
- Mapped to phases: 10
- Unmapped: 0

---
*Requirements defined: 2026-02-09*
*Last updated: 2026-02-10 after Phase 2 execution and verification*
