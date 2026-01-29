# Manual Test Plan

## Dataplane Validation
1. Connect the thermostat to the Home Assistant broker specified by `CONFIG_THEO_MQTT_HOST` and ensure the dataplane subscriptions are accepted.
2. Drag either setpoint slider on the device and release to commit. Watch the serial log for the `temperature_command` JSON publish and note the QoS1 message id.
3. Verify via `mosquitto_sub -t "${CONFIG_THEO_HA_BASE_TOPIC}/climate/theoretical_thermostat_climate_control/#" -v` (or HA's MQTT debug console) that the publishes land; the thermostat updates immediately and no timeout rollback should occur.
4. Observe the UI to ensure slider positions, weather, room, and HVAC indicators match the inbound MQTT payloads; malformed payloads should log warnings and show the ERR/error-color states without crashing.

## Remote Setpoint Animation
1. Let the panel enter idle sleep, then publish a new `target_temp_high` (cooling) via MQTT while monitoring logs. Confirm the controller logs the wake request, backlight-light transition, 1000 ms pre-delay, and that both tracks/labels animate toward the new value over ~1600 ms while the numerals show intermediate temperatures.
2. While the first animation is running, publish at least two additional setpoint updates (mix of `target_temp_low/high`). Confirm only the latest payload per burst runs next, there is no extra pre-delay, and the logs note the pending-session handoffs.
3. After the final animation, ensure the log reports a 1000 ms hold followed by `backlight_manager_schedule_remote_sleep(...)` only if the first wake was consumed and no other touches occurred. Trigger a touch during the hold to verify the auto-sleep is skipped.

## Boot Transition & UI Entrance Animations
1. Reboot the thermostat and observe the boot ceremony: sparkle drain → LED white fade-in (1200 ms) → white hold (1200 ms) → LED black fade-out (2000 ms).
2. Verify the splash screen remains fully visible during the LED white fade-in and hold, then fades out in sync with the LED black fade-out over 2000 ms.
3. Confirm the main UI entrance begins ~400 ms before the splash fade completes: top bar fades in left-to-right (weather → HVAC → room), cooling track grows, heating track follows 400 ms later, labels fade in (whole then fractional), then action bar icons fade in (mode → power → fan).
4. During the entrance animation, attempt to drag setpoints and tap action bar icons; verify no interactions occur. Once the fan icon fade completes, verify touch input works normally.
5. Tap the inactive setpoint label and the mode icon to toggle active target; confirm both setpoint label/track colors smoothly transition over ~300 ms without blocking interaction.
6. Enable quiet hours (covering current time), reboot, and confirm LED cues are suppressed while the splash fade and UI entrance animations still run.

## OTA Updates
1. Boot the thermostat and wait for the `OTA endpoint ready at http://<ip>:<port>/ota` log line.
2. Run `scripts/push-ota.sh` and confirm the OTA modal appears, the backlight stays on, the H.264 stream stops, and the device reboots after upload.
3. After reboot, confirm the log shows `[ota] running image marked valid` once the splash fades.
4. Verify missing length handling: `curl -X POST -H "Transfer-Encoding: chunked" http://<ip>:<port>/ota` returns HTTP 411.
5. Verify oversize handling: `dd if=/dev/zero of=/tmp/ota-oversize.bin bs=1M count=5` then `curl --data-binary @/tmp/ota-oversize.bin http://<ip>:<port>/ota` returns HTTP 413.

## Camera Streaming (WebRTC H.264 + Opus)
1. Wait for `WebRTC publisher started:` in the log; ensure the WHIP endpoint matches `CONFIG_THEO_WEBRTC_*` settings.
2. In go2rtc, verify the `thermostat` stream advertises `video: H264` and `audio: Opus` (16 kHz, mono). Example: `curl http://go2rtc/api/streams | jq '.thermostat.tracks'` should show `opus/16000/1` for audio.
3. Play the go2rtc stream with `ffplay rtsp://<go2rtc-host>:8554/thermostat` (or via the HA dashboard) and confirm room audio is audible without sustained silence or metallic artifacts.
4. Record a 15-second clip for ASR validation: `ffmpeg -i rtsp://<go2rtc-host>:8554/thermostat -map a -t 15 -c copy /tmp/thermostat_audio.opus`, then `ffmpeg -i /tmp/thermostat_audio.opus -ar 16000 -ac 1 -c:a pcm_s16le /tmp/thermostat_audio.wav` and feed it to the desktop ASR pipeline; capture WER/notes for the change record.
5. Disconnect the viewer and confirm the logs show `PeerConnectionState: disconnected` followed by `Stopping WebRTC publisher`; reconnect to ensure the session restarts automatically.
6. Attempt to start a second viewer while one is active; confirm go2rtc reports the thermostat stream as busy (single WebRTC session enforced) and the firmware keeps the first session running.
7. Temporarily build with `CONFIG_THEO_MICROPHONE_ENABLE=n` and confirm go2rtc now reports only the video track; verify the firmware logs that microphone streaming is disabled.

## Hundredth-Precision Setpoints
1. Wake the display and slowly drag each slider to land on a non-tenth value (e.g., 23.37 °C cooling, 21.82 °C heating). Watch the LVGL log for the `Committing setpoints` line and the `temperature_command` payload to confirm both emit two decimal places while the UI label continues to show tenths.
2. Publish `target_temp_high`/`target_temp_low` payloads that include hundredth precision (e.g., 25.55/22.15). Observe `[remote] animation started` logs to verify the controller accepts the hundredth value, and watch the animation to ensure the track motion appears continuous without 0.1 °C jumps while labels still round to tenths.

## LED Notifications & Quiet Hours
1. With `CONFIG_THEO_LED_ENABLE=y`, reboot the device and watch for the blue 0.33 Hz pulse immediately after reset plus the 1 s fade-up/hold/fade-down sequence once the splash dismisses. Confirm logs show `thermostat_led_status` transitions but boot continues even if LEDs fail.
2. Publish MQTT HVAC payloads (heat/cool) after the UI loads and confirm the diffuser switches to the specified colors (`#e1752e` heat, `#2776cc` cool) pulsing at 1 Hz; clear both states to verify a 100 ms fade-to-black. When LEDs are disabled via `CONFIG_THEO_LED_ENABLE=n`, confirm these requests log INFO about the kill switch without blocking boot.
3. Set `CONFIG_THEO_QUIET_HOURS_START_MINUTE`/`END_MINUTE` to cover the current local time, reboot, and wait for SNTP sync. Verify that new LED cues (heating/cooling) log WARN about quiet hours suppression until the window expires; also confirm the blue boot pulse still runs before the clock syncs and that once `thermostat_led_status_boot_complete()` fires, quiet-hours gating applies to subsequent requests.

## Scott Greeting Cues
1. Publish retained `homeassistant/sensor/hallway_camera_last_recognized_face/state` and `.../person_count/state` payloads (`Scott` and `0` respectively), then send a live `person_count` of `2` followed by a non-retained `Scott` face. Confirm the first payload is ignored, the live detection triggers exactly one greeting, and logs show both audio + LED cues.
2. Publish `homeassistant/sensor/hallway_camera_person_count/state` with `unavailable`, then send `Scott`. Verify WARN logs indicate suppression and no cue fires until a numeric count ≥1 arrives; after sending `1`, resend `Scott` and expect the greeting to run.
3. Force quiet hours (either via config or local time) and trigger a `Scott` detection. Confirm both audio and LED subsystems log suppression immediately and the helper resets so the next detection after quiet hours can run.
4. Start a conflicting LED effect (e.g., `rainbow` command) and then send `Scott`. Verify LED status logs a busy warning, the helper completes immediately, and audio still plays if permitted.
5. Build with `CONFIG_THEO_AUDIO_ENABLE=n`, trigger `Scott`, and confirm the helper logs an audio-disabled INFO but LEDs still run (subject to gating). Re-enable audio afterward.
6. Remove or rename `assets/audio/scott_greeting.wav` and run `scripts/generate_sounds.py`; confirm the script/build fails, documenting that the asset is required before attempting to flash.

## Environmental Telemetry

### Configuration Defaults
- `CONFIG_THEO_DEVICE_SLUG`: default `hallway`
- `CONFIG_THEO_DEVICE_FRIENDLY_NAME`: blank (auto-derives from slug as "Hallway")
- `CONFIG_THEO_THEOSTAT_BASE_TOPIC`: blank (auto-derives as `theostat`)
- `CONFIG_THEO_I2C_ENV_SDA_GPIO`: 7
- `CONFIG_THEO_I2C_ENV_SCL_GPIO`: 8
- `CONFIG_THEO_SENSOR_POLL_SECONDS`: 5
- `CONFIG_THEO_SENSOR_FAIL_THRESHOLD`: 3

### Boot & Sensor Initialization
1. Boot the thermostat with AHT20 and BMP280 sensors connected to GPIO7 (SDA) and GPIO8 (SCL). Confirm the splash screen shows "Starting environmental sensors..." and progresses past this stage without error.
2. Disconnect either sensor and reboot. Verify the boot halts at "start environmental sensors" with an error displayed on the splash screen.
3. After successful boot, check serial logs for `[env_sensors] AHT20 initialized` and `[env_sensors] BMP280 initialized` messages.

### MQTT Telemetry Publishing
1. Subscribe to `theostat/sensor/<slug>/#` using mosquitto_sub and verify:
   - Four state topics are published: `temperature_bmp/state`, `temperature_aht/state`, `relative_humidity/state`, `air_pressure/state`
   - Values are numeric with two decimal places
   - Messages are retained (QoS0)
2. Wait for `CONFIG_THEO_SENSOR_POLL_SECONDS` (default 5s) and confirm new values are published.
3. Disconnect MQTT broker temporarily; verify logs show "MQTT not ready, skipping state publish" warnings. Reconnect and confirm publishing resumes.

### Availability Topics
1. After boot, verify retained "online" messages on each `<TheoBase>/sensor/<slug>/<object_id>/availability` topic.
2. Simulate sensor failure by repeatedly failing reads (or disconnect sensor during runtime if hardware supports hot-plug). After `CONFIG_THEO_SENSOR_FAIL_THRESHOLD` consecutive failures, confirm:
   - Log shows "<object_id> marked offline after N consecutive failures"
   - Retained "offline" message appears on the availability topic
3. On successful read after being offline, confirm "online" is republished.

### Home Assistant Discovery
1. Subscribe to `homeassistant/sensor/<slug>/+/config` and verify four discovery payloads are published with correct:
   - `device_class`, `state_class`, `unit_of_measurement`
   - `unique_id` format: `theostat_<slug>_<object_id>`
   - `state_topic` and `availability_topic` paths
   - `device` block with `name`, `identifiers`, `manufacturer`, `model`
2. In Home Assistant, navigate to Settings > Devices & Services > MQTT and confirm the thermostat device appears with all four sensors.

### Temperature Command Topic Migration
1. Adjust a setpoint slider and release. Verify the command publishes to `<TheoBase>/climate/temperature_command` (not the old HA base topic).
2. Check logs for `temperature_command msg_id=X topic=theostat/climate/temperature_command`.
3. Confirm the UI still receives remote setpoint updates via the existing HA topic subscriptions.

## Presence Hold Cap
1. Set `CONFIG_THEO_BACKLIGHT_PRESENCE_MAX_SECONDS=60` for a quicker run.
2. Let the display sleep, then approach within wake distance and confirm presence wake.
3. Stay in place and wait for the cap; confirm logs show the hold exceeded message and the screen turns off.
4. Remain present and verify the screen stays off (presence ignored).
5. Step away until presence clears, then re-approach and confirm presence wake works again.
6. Repeat with a touch interaction ~40 seconds in to confirm the cap timer resets and extends the on duration.
