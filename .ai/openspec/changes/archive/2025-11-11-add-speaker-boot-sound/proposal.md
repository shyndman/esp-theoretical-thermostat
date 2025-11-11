# Add Speaker Boot Sound

## Problem
1. The thermostat boots silently; there is no audible confirmation the device started successfully or that the speaker hardware is wired correctly.
2. The Waveshare ESP32-P4-NANO BSP already exposes ES8311 speaker plumbing, but the app never initializes or drives it, so the capability sits idle.
3. Field reports note that users sometimes think the screen is frozen while the Wi-Fi stack comes up; a brief startup sound would improve perceived responsiveness.

## Why
1. A short boot cue reassures installers that the device powered on even when the display is still negotiating LVGL, reducing unnecessary troubleshooting.
2. Exercising the existing ES8311 path on every boot provides continuous hardware coverage, catching regressions earlier than sporadic manual tests.
3. Quiet-hours awareness prevents the new chime from becoming a nuisance in bedrooms or labs, keeping the feature shippable across deployments.

## Goals
1. Initialize the BSP speaker path during boot and verify playback works end-to-end.
2. Embed a short asset (≤1 second, ~16‑bit mono PCM) that plays exactly once after LVGL/backlight startup succeeds.
3. Expose a simple compile-time switch to mute the boot sound for lab testing, without introducing a runtime settings UI yet.
4. Respect configurable quiet hours (default overnight) by suppressing playback when the SNTP-synchronized wall clock indicates a “do not disturb” window.
5. Provide a Kconfig-controlled output volume (default mid-level) so labs can quiet the chime without rebuilding assets.

## Non-Goals
1. Do not add a generalized audio mixer, notification queue, or runtime volume settings—just the boot chime path.
2. Do not add microphone or voice capture support beyond what the BSP already provides.
3. Do not ship multiple audio assets or localization; single default clip is sufficient for now.

## Approach
1. Use `bsp_audio_codec_speaker_init()` plus `esp_codec_dev_write()` to play PCM from flash; no streaming or filesystem dependencies.
2. Convert the provided `assets/sound/ghost-laugh.wav` into a generated C array via `assets/sound/soundgen.toml` + `scripts/generate_sounds.py`, so `main/assets/audio/boot_chime.c` is compiled directly into the firmware and playback cannot fail due to missing files.
3. Gate playback behind a Kconfig option (default on) to keep CI quiet and give hardware bring-up teams a mute escape hatch.
4. Add Kconfig-driven quiet-hours start/end times (UTC offset via existing TZ setting) and check the SNTP wall clock before playback; fall back to “always play” until time sync succeeds.
5. Add a `CONFIG_THEO_BOOT_CHIME_VOLUME` (0–100) that maps to the codec’s output gain before playback.
6. Log WARN if audio init or playback fails but allow the UI to continue so boot never blocks on speaker issues.
7. Add spec coverage in a new `play-audio-cues` capability describing boot chime requirements, quiet hours, configurable volume, and failure handling.

## What Changes
1. Boot initializes the ES8311 speaker codec immediately after the backlight task and writes the embedded PCM array generated from `assets/sound/ghost-laugh.wav`.
2. New Kconfig switches handle enable/disable, quiet-hours start/end, and a 0–100 gain scalar that maps to the codec driver before playback.
3. Playback runs once per boot; failures emit WARN logs but never block UI startup, and missing time sync falls back to “always play” with explicit logging.
4. Documentation/spec coverage now lives in `spec/play-audio-cues`, detailing the runtime behavior, configuration hooks, and failure expectations for the boot chime.

## Risks & Mitigations
1. **Increased boot time**: Speaker init adds I2C/I2S configuration; keep it asynchronous (run right after backlight) and limit asset length to <1s.
2. **Binary size**: Raw PCM adds ~20 KB; keep sample rate 16 kHz mono and document limits in spec.
3. **Hardware variance**: Some deployments might lack speakers; ensure failures downgrade gracefully with WARN logs and no retries loops.
4. **Clock availability**: Quiet-hours enforcement depends on SNTP; play the chime (with a WARN) if the clock is unknown so we still get coverage on first boot while making it clear in logs why quiet hours were bypassed.
5. **Volume expectations**: Codec gain isn’t linear; document the 0–100 mapping in Kconfig help text and clamp out-of-range values before applying to avoid speaker pop or clipping.

## Validation Strategy
1. Unit-level smoke test via `idf.py build` to ensure the new asset and audio init compile on CI.
2. Manual hardware test: flash latest build, verify boot chime plays once during daytime hours, confirm chime suppression during quiet hours, and capture WARN log when speaker disconnected.
3. Document manual test steps in the PR description along with oscilloscope or log evidence, noting the timestamps used for quiet-hours verification.
