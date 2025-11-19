* [x] Update `play-audio-cues` spec to clarify that speaker initialization only occurs when `CONFIG_THEO_AUDIO_ENABLE = y` and that disabling the flag removes all codec/I2C activity.
* [x] Change `main/thermostat/audio_boot.c` so every entry point short-circuits before touching the codec when the flag is `n`, while preserving existing logging/policy when it is `y`.
* [x] Guard the boot-time calls in `main/app_main.c` so speaker preparation and playback helpers are only invoked when application audio is enabled.
* [x] Run `openspec validate remove-audio-init-when-disabled --strict` and a build (or equivalent compilation) to ensure the new guards compile cleanly.
