## ADDED Requirements

### Requirement: Anti-burn pixel training
The firmware SHALL provide an anti-burn “pixel training” mode that renders a static noise pattern over the full display for the configured anti-burn duration.

#### Scenario: Scheduled anti-burn begins
- **GIVEN** the anti-burn schedule window begins
- **AND** anti-burn is not already active
- **WHEN** the backlight manager enters anti-burn mode
- **THEN** the display shows a full-screen static noise effect (static-only)
- **AND** the effect updates continuously at best-effort display refresh cadence for the full configured duration.

#### Scenario: Anti-burn ends after duration elapses
- **GIVEN** anti-burn is active
- **WHEN** `CONFIG_THEO_ANTIBURN_DURATION_SECONDS` elapses
- **THEN** anti-burn stops automatically
- **AND** the static noise overlay is removed.

#### Scenario: Touch during anti-burn is ignored
- **GIVEN** anti-burn is active
- **WHEN** a touch interaction is detected
- **THEN** the touch is ignored/consumed (it does not trigger other UI actions)
- **AND** anti-burn continues to run until it stops via duration elapse, schedule window end, or explicit stop.

#### Scenario: Remote interaction behavior is unchanged
- **GIVEN** anti-burn is active
- **WHEN** non-touch remote flows call `backlight_manager_notify_interaction(BACKLIGHT_WAKE_REASON_REMOTE)`
- **THEN** behavior follows the existing backlight manager implementation.

### Requirement: Anti-burn brightness policy
During anti-burn, the backlight SHALL run at 100% brightness regardless of day/night mode, and SHALL restore the normal brightness policy after anti-burn stops.

#### Scenario: Anti-burn forces full brightness
- **GIVEN** the system is in night mode
- **WHEN** anti-burn starts
- **THEN** the backlight transitions to 100% brightness using the existing backlight transition behavior.

#### Scenario: Brightness restores after anti-burn
- **GIVEN** anti-burn is active at 100% brightness
- **WHEN** anti-burn stops (duration elapsed, schedule window end, or explicit stop)
- **THEN** the backlight returns to the expected day/night brightness target.

### Requirement: Anti-burn memory footprint
The anti-burn static noise effect SHALL NOT allocate or retain a screen-sized backing pixel buffer.

#### Scenario: Anti-burn starts on constrained memory
- **GIVEN** anti-burn starts
- **WHEN** the static overlay begins rendering
- **THEN** the firmware does not allocate a full-screen pixel buffer to support the effect.

### Requirement: Anti-burn static colors
The anti-burn static effect SHALL render per-pixel noise without tile quantization artifacts.

#### Scenario: Pixel colors are primary/white
- **GIVEN** anti-burn is active
- **WHEN** a pixel is drawn for the static effect
- **THEN** the pixel color is one of: pure red, pure green, pure blue, or white.
