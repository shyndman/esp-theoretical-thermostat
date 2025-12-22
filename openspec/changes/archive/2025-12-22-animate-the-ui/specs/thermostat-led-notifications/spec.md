## MODIFIED Requirements

### Requirement: Boot-phase LED cues
The boot sequence SHALL drive LEDs as follows: (a) while services start, display a sparkle animation that matches the reference Arduino sketch (frame cadence equivalent to 20 ms using two iterations of the existing 10 ms timer, `fadeToBlackBy(..., 9)` decay, up to four new sparkles per frame, and a spawn probability of 35/255 with pastel CHSV colors scaled to 20% intensity); (b) once boot succeeds, allow existing sparkles to fade out without spawning new ones, then perform a coordinated LED ceremony: fade to white over 1200ms, hold white for 1200ms, then fade to black over 2000ms synchronized with the splash screen fade-out.

#### Scenario: Boot in progress
- **WHEN** `app_main` begins booting subsystems (before Wi-Fi/time/MQTT are ready)
- **THEN** `thermostat_led_status_booting()` starts the sparkle animation, iterating twice per 10 ms tick so its fade/spawn pattern matches the Arduino scratch behavior, and keeps running until explicitly cleared.

#### Scenario: Boot success LED ceremony
- **WHEN** boot completes and the splash is ready to transition to main UI
- **THEN** the controller stops spawning new sparkles and allows existing sparkles to fade out
- **AND** once sparkles are fully faded, performs a white fade-in over 1200 ± 100 ms
- **AND** holds solid white for 1200 ± 100 ms while the splash screen remains visible
- **AND** fades to black over 2000 ± 100 ms, synchronized with the splash screen fade-out
- **AND** after the fade completes, hands control to HVAC state LED effects.

#### Scenario: LED ceremony uses centralized timing
- **WHEN** the boot success LED ceremony executes
- **THEN** all durations (white fade-in, white hold, black fade-out) are read from the centralized timing constants header
- **AND** changes to timing values in the header propagate to LED behavior without code changes.
