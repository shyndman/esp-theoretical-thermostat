## Implementation

- [ ] Create `scripts/generate_images.py` with uv script header and dependencies (tomli, resvg-py, Pillow)
- [ ] Implement `ImageJob` dataclass with source, size, derived symbol/outfile, usage fields
- [ ] Implement `load_manifest()` to parse `assets/images/imagegen.toml`
- [ ] Implement SVG-to-A8 rendering using resvg-py + Pillow alpha extraction
- [ ] Implement C file output matching existing `sunny.c` format
- [ ] Create initial `assets/images/imagegen.toml` manifest
- [ ] Test script generates identical output for existing sunny.svg at size 47