# Design: Weather Icon Composition

## Context
The thermostat displays weather conditions from an external API. The API returns condition codes that need visual representation. MDI provides a good base set of weather icons, but lacks:
- "Possible" variants with question badge indicators
- Sleet intensity variants

The MDI icons are 24x24 SVGs with single compound `<path>` elements.

## Goals
- Create all required weather icons
- Maintain visual consistency with MDI style
- Make composition repeatable/scriptable where practical

## Non-Goals
- Real-time icon generation (all icons pre-generated as SVG files)
- Animation support

## Decisions

### 1. Use `svgpathtools` for path manipulation
**Rationale**: Mature library that parses SVG path `d` attributes into objects, supports scale/translate transforms, and outputs clean path strings.

### 2. Extract atomic elements as Python constants
**Rationale**: Manually extract path segments once from MDI icons, store as named constants. Simpler than runtime SVG parsing.

**Atomic elements used**:
| Element | Source | Notes |
|---------|--------|-------|
| `CLOUD_WITH_GAP` | `lightning.svg` | Cloud positioned for weather below |
| `BOLT` | `lightning.svg` | Lightning bolt |
| `RAIN_DROP` | `rainy.svg` | Single teardrop |
| `SNOWFLAKE` | `snowy.svg` | Asterisk flake |
| `QUESTION_BADGE` | Google Material Symbols | Circle with question mark |

### 3. Question badge from Google Material Symbols
**Rationale**: MDI `help-circle.svg` didn't scale well. Google Material Symbols provided a cleaner filled circle with question mark cutout.

**Positioning**:
- 11px diameter badge
- 1px from bottom and right edges (center at 17.5, 17.5)
- 13px diameter background circle as first path element (halo/knockout effect)

### 4. Compose via transform + concatenate
SVG paths can be concatenated (space-separated `d` values). Apply scale/translate to each element, then join.

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

### 5. Manual editing for complex icons
Some icons required manual Inkscape work:
- **mostly-clear-day/night**: Sun+cloud and moon+stars+cloud compositions were too visually busy at actual display scale. Used simpler icons instead.
- **dangerous-wind**: Required boolean operations for alert triangle integration
- **smoke**: Google Material smoke trails needed precise positioning

### 6. Sleet intensity variants
| Condition | Composition |
|-----------|-------------|
| `very-light-sleet` | cloud + small drop + small flake |
| `light-sleet` | direct copy of `snowy-rainy.svg` |
| `heavy-sleet` | cloud + 2 drops + 2 flakes |

## Icon Inventory

### Script-generated: Direct copies (4)
| Output | Source |
|--------|--------|
| `breezy.svg` | `windy.svg` |
| `light-sleet.svg` | `snowy-rainy.svg` |
| `mist.svg` | `fog.svg` |
| `haze.svg` | `hazy.svg` |

### Script-generated: Aliases (4)
| Output | Source | Rationale |
|--------|--------|-----------|
| `mostly-cloudy-day.svg` | `partly-cloudy.svg` | Semantically equivalent |
| `mostly-cloudy-night.svg` | `night-partly-cloudy.svg` | Semantically equivalent |
| `drizzle.svg` | `rainy.svg` | Light rain visual |
| `flurries.svg` | `snowy.svg` | Light snow visual |

### Script-generated: Composed (12)
| Output | Composition |
|--------|-------------|
| `possible-rain-day.svg` | cloud + rain-drop + ?-badge |
| `possible-rain-night.svg` | cloud + rain-drop + ?-badge |
| `possible-snow-day.svg` | cloud + snowflake + ?-badge |
| `possible-snow-night.svg` | cloud + snowflake + ?-badge |
| `possible-sleet-day.svg` | cloud + drop + flake + ?-badge |
| `possible-sleet-night.svg` | cloud + drop + flake + ?-badge |
| `possible-precipitation-day.svg` | cloud + rain-drop + ?-badge |
| `possible-precipitation-night.svg` | cloud + rain-drop + ?-badge |
| `possible-thunderstorm-day.svg` | cloud + bolt + ?-badge |
| `possible-thunderstorm-night.svg` | cloud + bolt + ?-badge |
| `very-light-sleet.svg` | cloud + small drop + small flake |
| `heavy-sleet.svg` | cloud + 2 drops + 2 flakes |

### Manually created/edited (9)
| Output | Notes |
|--------|-------|
| `precipitation.svg` | Manual edit |
| `light-rain.svg` | Manual edit |
| `heavy-rain.svg` | Manual edit |
| `light-snow.svg` | Manual edit |
| `heavy-snow.svg` | Manual edit |
| `mostly-clear-day.svg` | Simpler icon (composition too busy) |
| `mostly-clear-night.svg` | Simpler icon (composition too busy) |
| `dangerous-wind.svg` | Inkscape boolean ops for alert triangle |
| `smoke.svg` | Google Material smoke trails, manually positioned |

## Lessons Learned

1. **Blind composition is hard**: Without visual feedback during path manipulation, multiple iterations were needed to get positioning right.
2. **Scale matters**: Icons that look good at 400% may be too busy at actual 24px display size.
3. **Know when to stop scripting**: Some icons are faster to create manually in Inkscape than to perfect in code.
