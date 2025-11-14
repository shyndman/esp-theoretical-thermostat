# Tasks
- [ ] Add a `CONFIG_THEO_LED_ENABLE` Kconfig + sdkconfig default that globally disables LED work when unset, mirroring the audio enable flag.
- [ ] Build the shared quiet-hours helper in `thermostat/application_cues.c` (public header + API described in design.md) so it owns the quiet window math, SNTP gate, and logging.
- [ ] Migrate existing audio boot logic (`audio_policy_check`, quiet math) to the new helper so boot/failure tones rely exclusively on the shared API.
- [ ] Integrate the helper into the LED path: the low-level `thermostat_leds_*` primitives must call it before arming timers, clear the strip when cues are disallowed, and re-check when quiet hours lift.
- [ ] Implement a `thermostat_leds` service that owns the `led_strip` handle (RMT backend, 6 RGBW pixels), caps updates to single-color fills, and offers `pulse(color, hz)`, `solid_with_fade(color, 100ms)` and `off_with_fade(100ms)` behaviors.
- [ ] Define a higher-level LED status controller that maps booting, booted, heating, and cooling events onto the service calls with the specified colors, fade profiles, and pulse rates (skip fan-only indication) and ensure it initializes before the display stack.
- [ ] Update boot + HVAC flows to trigger the controller at the right times, document quiet-hours coverage, and validate (`idf.py build` + hardware) that LEDs obey the shared gate and sequences end-to-end; capture steps in `docs/manual-test-plan.md`.
