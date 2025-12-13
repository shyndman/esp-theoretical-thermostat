## ADDED Requirements

### Requirement: Weather icon composition tool
The system SHALL provide a script `scripts/compose_weather_icons.py` that composes weather condition icons from atomic SVG path elements using `svgpathtools`.

#### Scenario: Compose icon from multiple elements
- **WHEN** the script defines an icon with multiple elements (path string, scale, translate_x, translate_y)
- **THEN** it applies transforms to each element and concatenates the paths into a single SVG

#### Scenario: Generate full weather icon set
- **WHEN** the script runs
- **THEN** it generates all 29 required weather icons to `assets/images/weather/`

### Requirement: Weather icon inventory
The system SHALL provide SVG icons for the following 29 weather conditions:

**Clear/cloudy variants (4):**
- `mostly-clear-day`, `mostly-clear-night`
- `mostly-cloudy-day`, `mostly-cloudy-night`

**Possible condition variants (10):**
- `possible-rain-day`, `possible-rain-night`
- `possible-snow-day`, `possible-snow-night`
- `possible-sleet-day`, `possible-sleet-night`
- `possible-precipitation-day`, `possible-precipitation-night`
- `possible-thunderstorm-day`, `possible-thunderstorm-night`

**Rain variants (4):**
- `precipitation`, `drizzle`, `light-rain`, `heavy-rain`

**Snow variants (3):**
- `flurries`, `light-snow`, `heavy-snow`

**Sleet variants (3):**
- `very-light-sleet`, `light-sleet`, `heavy-sleet`

**Wind variants (2):**
- `breezy`, `dangerous-wind`

**Visibility variants (3):**
- `mist`, `haze`, `smoke`

#### Scenario: All icons present
- **WHEN** the compose script completes
- **THEN** all 29 icon files exist in `assets/images/weather/`

#### Scenario: Icons follow MDI style
- **WHEN** any weather icon is rendered
- **THEN** it uses a 24x24 viewBox consistent with Material Design Icons
