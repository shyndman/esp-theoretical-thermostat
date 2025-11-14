# Change: Reorder boot to foreground splash + audio feedback

## Why
Users currently stare at a blank panel until every transport stack comes up. There is no indication of progress, and failures are silent unless someone watches the UART. We need a visible loading screen and audible failure tone to make bring-up diagnosable without a debugger.

## What Changes
- Bring up the LCD, LVGL adapter, and backlight first, then render a simple loading screen.
- Update that screen with "Verbing <stage>..." status text for every service spin-up.
- Initialize the speaker codec immediately after the display and reuse it for either the boot chime (success) or a new failure tone if any stage aborts the boot.
- Respect existing quiet-hour logic for all audio cues and ensure boot failures always play `main/assets/audio/failure.c` when not muted.

## Impact
- Affected specs: thermostat-boot-experience (new), play-audio-cues (modified)
- Affected code: `main/app_main.c`, LVGL splash helper, `thermostat/audio_boot.*`, failure asset wiring
