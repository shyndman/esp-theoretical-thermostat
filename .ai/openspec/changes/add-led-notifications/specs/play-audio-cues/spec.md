## MODIFIED Requirements
### Requirement: Quiet Hours Suppression
The firmware MUST suppress all boot-time audio cues (boot chime and failure tone) during configurable quiet hours and whenever the SNTP clock has not synchronized yet. The same quiet-hours gate SHALL be implemented as a shared helper consumed by every "application cue" subsystem, starting with the new LED diffuser notifications, so LEDs and audio always enable/disable together.

#### Scenario: Quiet hours active
- **WHEN** local time falls within the configured quiet window and the clock is synchronized
- **THEN** both the boot chime/failure tone and any LED notification requests are skipped, with WARN logs documenting the suppression.

#### Scenario: Clock unsynchronized
- **WHEN** quiet hours are configured and the device has not synchronized time (or time sync failed)
- **THEN** the shared cue gate refuses audio and LED output, logs WARN that application cues are disabled until the clock syncs, and leaves both the codec and LED strip idle.
