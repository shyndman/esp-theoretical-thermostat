# Tasks
- [ ] Stage remote setpoint requests via a controller that prevents direct LVGL writes inside the MQTT handler.
- [ ] Expose or add backlight helpers so the controller can detect when a `BACKLIGHT_WAKE_REASON_REMOTE` call actually lit the panel.
- [ ] Orchestrate the session timeline: for the first remote burst wait until lit, delay 1000 ms, animate both sliders together with 1600 ms ease-in-out while updating label text continuously, delay 1000 ms, then schedule remote sleep only if we woke the panel; each subsequent session in the burst should skip the pre-delay but still poke the backlight activity hook.
- [ ] Handle multiple remote updates by coalescing into a single pending session (latest wins) so animations never overlap or snap mid-flight, and document how pending sessions replace each other until the burst drains.
- [ ] Validate on hardware/emulator that the wake-delay-animate-hold-sleep sequence runs with the intended pacing and logs.
