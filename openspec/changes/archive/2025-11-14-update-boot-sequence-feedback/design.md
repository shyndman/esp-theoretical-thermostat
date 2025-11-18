# Design Notes

## Boot Flow Ordering
1. **Display & LVGL First:** Move BSP display + LVGL adapter + backlight to the top of `app_main`. Immediately lock LVGL and draw a minimalist splash layer (dark background + centered status text). Keep the splash screen object alive until the main UI attaches.
2. **Speaker Prep Second:** Add `thermostat_audio_boot_prepare()` that initializes the codec + volume without playing audio. This runs right after LVGL/backlight so future cues (success or failure) are instant.
3. **Sequenced Services:** Wrap each subsystem init (esp-hosted link, Wi-Fi, SNTP/timesync, MQTT manager, MQTT dataplane) with `splash_set_status("Verbing ...")` before the call. On success, continue; on failure, jump to a shared `boot_fail()` helper.
4. **Failure Handling:** `boot_fail(stage_name, err)` updates the splash with an error message, attempts the failure tone (honoring quiet hours), logs, and then blocks (e.g., infinite delay) so the message stays visible.
5. **Success Path:** Once MQTT dataplane is up, release the splash, attach the main thermostat UI, signal `backlight_manager_on_ui_ready()`, then call `thermostat_audio_boot_try_play()`.

## Audio Changes
- Share codec handles between prepare/boot chime/failure tone so we never re-initialize hardware mid-boot.
- Replace the current "play anyway if the clock is unsynced" logic with a strict audio policy helper: sounds only play when (a) `CONFIG_THEO_AUDIO_ENABLE = y`, (b) SNTP reports synchronized time, and (c) the current minute is outside the quiet-hours window. There is no fallback playback.
- All application sounds (boot chime, failure tone, future UX cues) must go through this helper. Safety sirens/warnings, if added later, bypass the helper so they cannot be silenced via config.
- Failure tone uses `sound_failure` PCM and logs WARN whenever policy suppression or codec writes fail.

## Quiet Hours and Logging
- Because the audio policy enforces the same guard for every cue, quiet-hour skips always log WARN with the reason (quiet window or unsynced clock). Successful plays log INFO with byte counts.
- Splash text should include the stage verb and, on failure, the error name so field techs can correlate without a console.
