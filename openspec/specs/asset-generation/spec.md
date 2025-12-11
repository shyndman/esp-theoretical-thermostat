# asset-generation Specification

## Purpose
TBD - created by archiving change generate-images. Update Purpose after archive.
## Requirements
### Requirement: Image generation from SVG sources
The system SHALL provide a script `scripts/generate_images.py` that converts SVG files to LVGL-compatible C source files based on a TOML manifest.

#### Scenario: Generating a weather icon
- **WHEN** `assets/images/imagegen.toml` contains an entry `{ source = "sunny.svg", size = 47 }`
- **AND** `assets/images/sunny.svg` exists
- **THEN** running `scripts/generate_images.py` produces `main/assets/images/sunny_47.c`
- **AND** the output contains a `uint8_t sunny_47_map[]` array with A8 pixel data
- **AND** the output contains an `lv_image_dsc_t sunny_47` descriptor with correct dimensions

#### Scenario: Multiple sizes from one source
- **WHEN** the manifest contains two entries for `sunny.svg` with sizes 47 and 128
- **THEN** the script produces both `sunny_47.c` and `sunny_128.c`

### Requirement: Manifest-driven configuration
The script SHALL read image definitions from `assets/images/imagegen.toml` using a `[[image]]` table array.

#### Scenario: Minimal manifest entry
- **GIVEN** a manifest entry with only `source` and `size` fields
- **THEN** `symbol` is derived as `{stem}_{size}` (e.g., `sunny_47`)
- **AND** `outfile` is derived as `{stem}_{size}.c`

#### Scenario: Optional usage field
- **WHEN** an entry includes `usage = "Weather icon"`
- **THEN** the script logs this description during generation

### Requirement: A8 color format output
All generated images SHALL use `LV_COLOR_FORMAT_A8` (8-bit alpha channel) to match existing icon assets.

#### Scenario: Alpha channel extraction
- **GIVEN** an SVG with anti-aliased shapes on transparent background
- **WHEN** rendered at the specified size
- **THEN** only the alpha channel is extracted as pixel data
- **AND** stride equals width (1 byte per pixel)

### Requirement: LVGL C file structure
Generated C files SHALL match the structure of existing image assets in `main/assets/images/`.

#### Scenario: File structure verification
- **GIVEN** a generated image file
- **THEN** it includes `#include "lvgl.h"`
- **AND** defines `LV_ATTRIBUTE_MEM_ALIGN` and `LV_ATTRIBUTE_{SYMBOL}` guards
- **AND** declares a static `uint8_t {symbol}_map[]` array
- **AND** declares a `const lv_image_dsc_t {symbol}` with header fields: magic, cf, flags, w, h, stride, reserved_2

