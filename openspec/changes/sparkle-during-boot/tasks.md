## 1. Implementation
- [ ] 1.1 Update `thermostat_leds` initialization to assume 39 pixels, document the left/top/right ordering, and keep global brightness scaling.
- [ ] 1.2 Introduce a sparkle effect state (pixel buffer, fade-by 9, spawn ≤4 pastel sparkles per 20 ms) that mirrors the Arduino sketch timing while still using the existing 10 ms esp_timer tick.
- [ ] 1.3 Replace the boot pulse in `thermostat_led_status` with the sparkle effect, keeping the existing boot-complete fade/hold/fade sequence.
- [ ] 1.4 Run `idf.py build` (or the relevant component tests) to ensure the firmware compiles with the new effect.
