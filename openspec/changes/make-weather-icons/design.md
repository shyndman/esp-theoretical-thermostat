# Design: Weather Icon Composition

## Context
The thermostat displays weather conditions from an external API. The API returns condition codes that need visual representation. MDI provides a good base set of weather icons, but lacks:
- Day/night variants with "possible" indicators (? badge)
- Intensity variants (drizzle vs light-rain vs heavy-rain)
- Specific conditions (smoke with puffs vs haze with swirlies)

The MDI icons are 24x24 SVGs with single compound `<path>` elements.

## Goals
- Create all 29 required weather icons
- Maintain visual consistency with MDI style
- Make composition repeatable/scriptable

## Non-Goals
- Real-time icon generation (all icons pre-generated as SVG files)
- Animation support

## Decisions

### 1. Use `svgpathtools` for path manipulation
**Rationale**: Mature library that parses SVG path `d` attributes into objects, supports scale/translate transforms, and outputs clean path strings. Avoids writing custom path math.

### 2. Extract atomic elements as Python constants
**Rationale**: Manually extract path segments once from MDI icons, store as named constants. Simpler than runtime SVG parsing with element identification logic.

**Atomic elements needed**:
| Element | Source | Notes |
|---------|--------|-------|
| `MOON_CRESCENT` | `night.svg` | Large crescent, needs scaling for corner placement |
| `SUN_PEEKING` | `partly-cloudy.svg` | Sun peeking from behind cloud |
| `CLOUD_FULL` | `cloudy.svg` | Full cloud |
| `CLOUD_WITH_GAP` | `lightning.svg` | Cloud positioned for weather below |
| `BOLT` | `lightning.svg` | Lightning bolt only |
| `RAIN_DROP` | `rainy.svg` | Single teardrop |
| `RAIN_POUR` | `pouring.svg` | Multiple streaming lines |
| `SNOWFLAKE` | `snowy.svg` | Single asterisk flake |
| `SNOWFLAKE_HEAVY` | `snowy-heavy.svg` | Two flakes |
| `SLEET_MIX` | `snowy-rainy.svg` | Drop + flake combo |
| `WIND_LINES` | `windy.svg` | Three curved lines |
| `QUESTION_BADGE` | MDI `help-circle.svg` | Circled ? for corner badge |
| `CLOUD_TINY` | Derived | Small cloud for "mostly clear" |

### 3. Compose via transform + concatenate
**Rationale**: SVG paths can be concatenated (space-separated `d` values). Apply scale/translate to each element, then join.

```python
def compose(elements: list[tuple[str, float, float, float]]) -> str:
    """elements: list of (path_d, scale, translate_x, translate_y)"""
    parts = []
    for d, scale, tx, ty in elements:
        path = parse_path(d)
        if scale != 1.0:
            path = path.scaled(scale)
        if tx or ty:
            path = path.translated(complex(tx, ty))
        parts.append(path.d())
    return " ".join(parts)
```

### 4. Question badge style: circled `?` in lower-right corner
**Rationale**: Matches Google Material Symbols "Unknown Document" pattern. Scale `help-circle.svg` to ~35% and position at approximately (16, 16) origin.

### 5. "Mostly clear" = celestial + tiny cloud
**Rationale**: Differentiates from plain "clear" by adding small cloud hint.

### 6. Intensity variants are visually distinct
| Condition | Light | Medium | Heavy |
|-----------|-------|--------|-------|
| Rain | 1 small drop | 1 large drop (default `rainy.svg`) | 3 streaming lines (`pouring.svg`) |
| Snow | 1 small flake | 1 large flake (default `snowy.svg`) | 2 flakes (`snowy-heavy.svg`) |
| Sleet | small drop+flake | default `snowy-rainy.svg` | 2 drops + 2 flakes |

### 7. Smoke vs Haze
- **Haze**: Uses existing `hazy.svg` (sun + horizontal lines = swirlies)
- **Smoke**: New composition with cloud + puff shapes (rounded billows)

## Icon Inventory

### Direct copies (8)
| Required | Source |
|----------|--------|
| `precipitation` | `rainy.svg` |
| `heavy-rain` | `pouring.svg` |
| `light-snow` | `snowy.svg` |
| `heavy-snow` | `snowy-heavy.svg` |
| `light-sleet` | `snowy-rainy.svg` |
| `breezy` | `windy.svg` |
| `mist` | `fog.svg` |
| `haze` | `hazy.svg` |

### Shared icons (5)
| Required | Uses same as | Rationale |
|----------|--------------|-----------|
| `mostly-cloudy-day` | `partly-cloudy.svg` | Semantically equivalent |
| `mostly-cloudy-night` | `night-partly-cloudy.svg` | Semantically equivalent |
| `drizzle` | `rainy.svg` | Light rain visual |
| `light-rain` | `rainy.svg` | Same as precipitation |
| `flurries` | `snowy.svg` | Light snow visual |

### Composed icons (16)
| Required | Composition |
|----------|-------------|
| `mostly-clear-day` | sun + tiny cloud |
| `mostly-clear-night` | moon + tiny cloud |
| `possible-rain-day` | sun-peeking + cloud + rain-drop + ?-badge |
| `possible-rain-night` | moon + cloud + rain-drop + ?-badge |
| `possible-snow-day` | sun-peeking + cloud + snowflake + ?-badge |
| `possible-snow-night` | moon + cloud + snowflake + ?-badge |
| `possible-sleet-day` | sun-peeking + cloud + sleet-mix + ?-badge |
| `possible-sleet-night` | moon + cloud + sleet-mix + ?-badge |
| `possible-precipitation-day` | sun-peeking + cloud + rain-drop + ?-badge |
| `possible-precipitation-night` | moon + cloud + rain-drop + ?-badge |
| `possible-thunderstorm-day` | sun-peeking + cloud + bolt + ?-badge |
| `possible-thunderstorm-night` | moon + cloud + bolt + ?-badge |
| `very-light-sleet` | cloud + small drop + small flake |
| `heavy-sleet` | cloud + 2 drops + 2 flakes |
| `dangerous-wind` | wind-lines + alert indicator |
| `smoke` | cloud + puff shapes |

## Risks / Trade-offs

- **Manual path extraction**: One-time tedious work, but avoids complex parsing logic
- **Scaling artifacts**: Bezier curves may look slightly different at small scales; validate visually
- **Consistency**: Composed icons may not perfectly match MDI style; may need manual tweaks

## Open Questions

- [RESOLVED] Question badge style: Use circled `?` from `help-circle.svg`
- [RESOLVED] Night indicator: Use moon crescent (clearest)
- [RESOLVED] Smoke shape: Puffs (different from haze swirlies)
