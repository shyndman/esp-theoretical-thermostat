## Context
The OV5647 sensor outputs RAW Bayer data that the ESP ISP must convert to YUV/RGB before the hardware H.264 encoder consumes it. Today we skip ISP configuration, so the ISP leaves most stages disabled and hands the encoder a luminance-only surface. The fix requires coordinating `esp_video` (ISP controller), the V4L2 nodes exposed by ESP-IDF, and our streaming pipeline.

## Goals / Non-Goals
- **Goals**: enable ISP stages that restore color, expose tunables, keep the pipeline resilient if controls are missing.
- **Non-Goals**: implement auto-focus or per-scene calibration, change encoder settings beyond what’s required for color.

## Decisions
1. **Configure ISP via V4L2 controls** – Use `VIDIOC_S_EXT_CTRLS` with the ESP-specific control IDs (CCM, GAMMA, WB, DEMOSAIC) so we don’t reimplement ISP register pokes. This keeps us aligned with upstream ESP-IDF.
2. **Hook after capture init** – Call the ISP helper once `esp_video` and `/dev/video0` are ready but before we enqueue frames, ensuring the encoder always sees colorized buffers.
3. **Kconfig-backed tunables** – Define Kconfig entries for key gains (red/blue, saturation, gamma preset) so deployments can tweak without recompiling C.

## Risks / Trade-offs
- **Control availability**: early ESP-IDF drops may lack some ISP ioctl IDs. Mitigation: treat `-ENOTTY`/`-EINVAL` as warnings and continue streaming.
- **Performance**: enabling ISP stages adds processing latency. Expect a few milliseconds per frame, acceptable for the thermostat scenario.

## Migration Plan
1. Implement helper and feature flag it behind `CONFIG_THEO_CAMERA_ENABLE`.
2. Roll firmware to a bench unit, gather sample frames.
3. Adjust Kconfig defaults if colors skew, then roll broadly.

## Open Questions
- Do we need per-environment presets (day vs night)? (Out of scope for now.)
