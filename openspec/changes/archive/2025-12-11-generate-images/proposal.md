# Change: Add SVG-to-LVGL image generation script

## Why
The codebase has `generate_fonts.py` and `generate_sounds.py` for manifest-driven asset generation, but images are manually converted and committed. This creates friction when adding or resizing icons and risks inconsistency in output format.

## What Changes
- Add `scripts/generate_images.py` to convert SVG sources to LVGL-compatible C files
- Add `assets/images/imagegen.toml` manifest defining source SVGs and output sizes
- Output format matches existing `main/assets/images/*.c` files (A8 color format)

## Impact
- Affected specs: `asset-generation` (new capability)
- Affected code: `scripts/`, `assets/images/`, `main/assets/images/`