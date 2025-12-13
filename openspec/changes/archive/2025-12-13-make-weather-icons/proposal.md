# Change: Create complete weather icon set via SVG composition

## Why
The thermostat UI needs a standardized set of weather condition icons. The existing `assets/images/` directory has MDI weather icons that cover some conditions, but many are missing (especially "possible" variants with question badges and sleet intensity variants).

## What Changes
- Add `scripts/compose_weather_icons.py` tool using `svgpathtools` to transform and combine SVG path elements
- Define atomic path constants extracted from MDI sources and Google Material Symbols
- Generate weather icons to `assets/images/weather/` subdirectory:
  - 4 direct copies from existing MDI icons
  - 4 aliases for semantically equivalent conditions
  - 12 composed icons (10 possible-* variants + 2 sleet intensity variants)
- Several icons manually created/edited in Inkscape where composition was insufficient

## Impact
- Affected code: `scripts/`, `assets/images/`
- Dependencies: `svgpathtools` Python package (via uv script header)
