## MODIFIED Requirements
### Requirement: Quiet Hours Suppression
The firmware MUST suppress all boot-time audio cues (boot chime and failure tone) during configurable quiet hours (`CONFIG_THEO_QUIET_HOURS_START_MINUTE` / `CONFIG_THEO_QUIET_HOURS_END_MINUTE`) and whenever the SNTP clock has not synchronized yet. The same quiet-hours gate SHALL be implemented as a shared helper consumed by every "application cue" subsystem, starting with the new LED diffuser notifications, so LEDs and audio always apply identical request-time checks.

#### Scenario: Quiet hours active
- **WHEN** a cue request (audio or LED) arrives while local time falls within the configured quiet window and the clock is synchronized
- **THEN** that request is skipped, with WARN logs documenting the suppression.

#### Scenario: Clock unsynchronized
- **WHEN** quiet hours are configured and the device has not synchronized time (or time sync failed)
- **THEN** the shared cue gate refuses audio and LED output, logs WARN that application cues are disabled until the clock syncs, and leaves both the codec and LED strip idle. LED boot cues may bypass this gate until SNTP supplies time so early-boot status is still visible; once time is available the helper enforces quiet hours uniformly.
