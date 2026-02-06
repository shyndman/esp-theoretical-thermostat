## 1. Implementation
- [x] 1.1 Remove the canvas-backed snow buffer allocation from `main/thermostat/backlight_manager.c`.
- [x] 1.2 Implement the anti-burn “static” effect as an LVGL draw-time overlay (no screen-sized backing buffer) using `LV_EVENT_DRAW_MAIN` + `lv_event_get_layer()`.
- [x] 1.3 Render per-pixel primary/white static (no tile quantization), and iterate pixels in clip order.
- [x] 1.4 Ensure anti-burn forces brightness to 100% for the full duration, then restores normal brightness policy on exit.

## 2. Validation
- [x] 2.1 `idf.py build`.
- [ ] 2.2 On-device: trigger anti-burn manually and verify:
  - the display jumps to 100% brightness via the normal fade,
  - static noise animates continuously (static-only; no other patterns),
  - each pixel is red/green/blue/white (no blocky tiling),
  - no full-screen snow buffer allocation occurs,
  - touches do not trigger UI actions during anti-burn,
  - at duration end, anti-burn stops and brightness returns to the expected day/night value.
