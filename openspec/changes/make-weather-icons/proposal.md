# Change: Create complete weather icon set via SVG composition

## Why
The thermostat UI needs a standardized set of 29 weather condition icons. The existing `assets/images/` directory has MDI weather icons that cover some conditions, but many are missing (especially day/night "possible" variants and intensity variants). Rather than manually editing SVGs, we can compose new icons from atomic elements extracted from existing MDI icons.

## What Changes
- Add `scripts/compose_weather_icons.py` tool using `svgpathtools` to transform and combine SVG path elements
- Define atomic path constants (moon, sun, cloud, bolt, rain drop, snowflake, question badge, etc.) extracted from MDI sources
- Generate 29 weather icons:
  - 8 direct copies/renames from existing MDI icons
  - 5 reused icons for similar conditions (e.g., `drizzle` = `rainy.svg`)
  - 16 composed icons (mostly-clear variants, all possible-* variants, intensity variants, smoke)
- Output SVGs to `assets/images/weather/` subdirectory

## Impact
- Affected specs: `asset-generation` (extends existing capability from `generate-images` change)
- Affected code: `scripts/`, `assets/images/`
- Dependencies: `svgpathtools` Python package
