# Add Speaker Boot Sound

## Problem
1. The thermostat boots silently; there is no audible confirmation the device started successfully or that the speaker hardware is wired correctly.
2. The Waveshare ESP32-P4-NANO BSP already exposes ES8311 speaker plumbing, but the app never initializes or drives it, so the capability sits idle.
3. Field reports note that users sometimes think the screen is frozen while the Wi-Fi stack comes up; a brief startup sound would improve perceived responsiveness.

## Goals
1. Initialize the BSP speaker path during boot and verify playback works end-to-end.
2. Embed a short asset (≤1 second, ~16‑bit mono PCM) that plays exactly once after LVGL/backlight startup succeeds.
3. Expose a simple compile-time switch to mute the boot sound for lab testing, without introducing a runtime settings UI yet.

## Non-Goals
1. Do not add a generalized audio mixer, notification queue, or runtime volume settings—just the boot chime path.
2. Do not add microphone or voice capture support beyond what the BSP already provides.
3. Do not ship multiple audio assets or localization; single default clip is sufficient for now.

## Approach
1. Use `bsp_audio_codec_speaker_init()` plus `esp_codec_dev_write()` to play PCM from flash; no streaming or filesystem dependencies.
2. Store the boot asset under `main/assets/audio/boot_chime.raw` and convert it into a compiled-in array with CMake so playback cannot fail due to missing files.
3. Gate playback behind a Kconfig option (default on) to keep CI quiet and give hardware bring-up teams a mute escape hatch.
4. Log WARN if audio init or playback fails but allow the UI to continue so boot never blocks on speaker issues.
5. Add spec coverage in a new `play-audio-cues` capability describing boot chime requirements and failure handling.

## Risks & Mitigations
1. **Increased boot time**: Speaker init adds I2C/I2S configuration; keep it asynchronous (run right after backlight) and limit asset length to <1s.
2. **Binary size**: Raw PCM adds ~20 KB; keep sample rate 16 kHz mono and document limits in spec.
3. **Hardware variance**: Some deployments might lack speakers; ensure failures downgrade gracefully with WARN logs and no retries loops.

## Validation Strategy
1. Unit-level smoke test via `idf.py build` to ensure the new asset and audio init compile on CI.
2. Manual hardware test: flash latest build, verify boot chime plays once, confirm WARN log appears if speaker disconnected.
3. Document manual test steps in the PR description along with oscilloscope or log evidence.
