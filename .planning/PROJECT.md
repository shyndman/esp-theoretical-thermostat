# ESP Theoretical Thermostat Reliability Hardening

## What This Is

This project is the next milestone for the existing ESP32-P4 thermostat firmware. We are not building a new product; we are hardening the current system so it stays stable under long uptime and constrained internal RAM conditions. The immediate focus is reliability and resource safety in the live, single-device deployment.

## Core Value

The thermostat must remain stable and responsive over long runtime on real hardware, with internal RAM safety treated as a first-class requirement.

## Requirements

### Validated

- ✓ Touchscreen thermostat UI with MQTT-backed state updates and setpoint control — existing
- ✓ Environmental sensing telemetry pipeline (AHT20/BMP280 + MQTT publish) — existing
- ✓ LAN WebRTC streaming pipeline with WHEP signaling — existing
- ✓ Boot orchestration with splash feedback, OTA flow, and subsystem bring-up — existing

### Active

- [ ] Replace MQTT fragment reassembly dynamic allocation path with serialized handling and a fixed pre-allocated buffer in internal-RAM-aware design
- [ ] Add stack high-water monitoring and enforce safety thresholds for critical tasks
- [ ] Upgrade heap fragmentation monitoring from periodic logs to actionable thresholds and alerts
- [ ] Fix radar start timeout task/context lifetime race in boot path

### Out of Scope

- Hardware portability refactors for alternate boards/display geometries — not needed for this fixed single-device deployment
- MQTT command-topic authentication/signing — currently out of scope for this LAN-trusted setup
- LVGL lock-contention optimization — explicitly deferred unless profiling proves user-visible impact
- MQTT topic-length expansion and generalized scaling work — explicitly deprioritized for current deployment

## Context

The codebase is brownfield and already shipping integrated UI, connectivity, sensors, and streaming subsystems on ESP-IDF 5.5.x. Recent code mapping surfaced multiple concerns; owner triage marked internal-RAM allocation behavior, task stack safety, and heap fragmentation observability as immediate. Other items (for example splash complexity and WebRTC lifecycle complexity) remain relevant but are not immediate for this milestone.

## Constraints

- **Hardware**: Single pre-built FireBeetle 2 ESP32-P4 thermostat target — no multi-device scaling requirement right now
- **Memory**: Internal RAM headroom is critical — avoid frequent hot-path dynamic allocation where practical
- **Network**: LAN-only operation for WebRTC/MQTT topology assumptions
- **Architecture**: Existing ESP-IDF + LVGL + ESP-Hosted stack remains in place — hardening over rewrite

## Key Decisions

| Decision | Rationale | Outcome |
|----------|-----------|---------|
| Prioritize internal RAM safety first | Most direct stability risk for long uptime | — Pending |
| Treat MQTT payload correctness as secondary to memory behavior | Data loss is acceptable compared to instability | — Pending |
| Defer non-immediate complexity cleanups | Keep scope on immediate reliability wins | — Pending |

---
*Last updated: 2026-02-09 after initialization*
