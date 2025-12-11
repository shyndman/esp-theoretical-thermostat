## Implementation

- [x] Create `scripts/generate_images.py` with uv script header and dependencies (tomli, resvg-py, Pillow)
- [x] Implement `ImageJob` dataclass with source, size, derived symbol/outfile, usage fields
- [x] Implement `load_manifest()` to parse `assets/images/imagegen.toml`
- [x] Implement SVG-to-A8 rendering using resvg-py + Pillow alpha extraction
- [x] Implement C file output matching existing `sunny.c` format
- [x] Create initial `assets/images/imagegen.toml` manifest
- [x] Test script generates identical output for existing sunny.svg at size 47