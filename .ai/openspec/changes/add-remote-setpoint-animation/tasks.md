# Tasks
- [ ] Stage remote setpoint requests via a controller that prevents direct LVGL writes inside the MQTT handler.
- [ ] Expose or add backlight helpers so the controller can detect when a `BACKLIGHT_WAKE_REASON_REMOTE` call actually lit the panel.
- [ ] Orchestrate the timeline: wait until lit, delay 1000 ms, animate sliders with 1600 ms ease-in-out, delay 1000 ms, then schedule remote sleep only if we woke the panel.
- [ ] Handle multiple remote updates by queuing/coalescing so animations never overlap or snap mid-flight, and document cancellation semantics.
- [ ] Validate on hardware/emulator that the wake-delay-animate-hold-sleep sequence runs with the intended pacing and logs.
