## MODIFIED Requirements

### Requirement: Boot Chime Playback
A compiled-in PCM asset MUST play exactly once every boot when application audio is enabled and all boot stages succeed, timed to the peak of the boot white-out.

#### Scenario: Boot chime at LED white peak
- **WHEN** the LED success sequence reaches the end of the white fade-in (screen and LEDs fully white)
- **THEN** the firmware plays the embedded `boot_chime` buffer exactly once
- **AND** playback finishes within 2 s without blocking the UI loop, logging WARN if the audio-pipeline write fails
- **AND** playback remains subject to quiet-hours suppression and unsynchronized-clock gating.
