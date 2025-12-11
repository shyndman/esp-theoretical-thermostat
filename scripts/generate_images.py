#!/usr/bin/env -S uv run --script
# /// script
# requires-python = "~=3.11"
# dependencies = [
#   "tomli>=2.3.0",
#   "resvg-py>=0.2.4",
#   "Pillow>=11.0.0",
# ]
# ///
"""Convert SVG sources to LVGL-compatible C files (A8 format)."""

from __future__ import annotations

import io
from dataclasses import dataclass
from pathlib import Path

import resvg_py
import tomli
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "assets" / "images" / "imagegen.toml"
SRC_DIR = ROOT / "assets" / "images"
OUT_DIR = ROOT / "main" / "assets" / "images"


@dataclass
class ImageJob:
    source: Path
    size: int
    symbol: str
    outfile: Path
    usage: str


def load_manifest() -> list[ImageJob]:
    if not MANIFEST.exists():
        raise SystemExit(f"Manifest missing: {MANIFEST}")
    data = tomli.loads(MANIFEST.read_text())
    jobs: list[ImageJob] = []
    for entry in data.get("image", []):
        source = entry["source"]
        size = int(entry["size"])
        # Derive symbol from source basename if not specified
        symbol = entry.get("symbol") or Path(source).stem.replace("-", "_")
        # Derive outfile from symbol if not specified
        outfile = entry.get("outfile") or f"{symbol}.c"
        job = ImageJob(
            source=SRC_DIR / source,
            size=size,
            symbol=symbol,
            outfile=OUT_DIR / outfile,
            usage=str(entry.get("usage", "")),
        )
        jobs.append(job)
    return jobs


def render_svg_to_alpha(svg_path: Path, size: int) -> bytes:
    """Render SVG to A8 (alpha-only) pixel data."""
    png_bytes = resvg_py.svg_to_bytes(
        svg_path=str(svg_path),
        width=size,
        height=size,
    )
    img = Image.open(io.BytesIO(bytes(png_bytes)))
    alpha = img.convert("RGBA").split()[3]
    return alpha.tobytes()


def format_pixel_data(data: bytes, width: int) -> str:
    """Format pixel data as C array rows, one row per line."""
    lines: list[str] = []
    for row_start in range(0, len(data), width):
        row = data[row_start : row_start + width]
        hex_vals = ",".join(f"0x{b:02x}" for b in row)
        lines.append(f"    {hex_vals},")
    return "\n".join(lines)


def generate_c_file(job: ImageJob, pixel_data: bytes) -> str:
    """Generate LVGL-compatible C file content."""
    symbol = job.symbol
    symbol_upper = symbol.upper()
    size = job.size

    pixel_array = format_pixel_data(pixel_data, size)

    return f"""
#include "lvgl.h"

#ifndef LV_ATTRIBUTE_MEM_ALIGN
#define LV_ATTRIBUTE_MEM_ALIGN
#endif

#ifndef LV_ATTRIBUTE_{symbol_upper}
#define LV_ATTRIBUTE_{symbol_upper}
#endif

static const
LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_LARGE_CONST LV_ATTRIBUTE_{symbol_upper}
uint8_t {symbol}_map[] = {{

{pixel_array}

}};

const lv_image_dsc_t {symbol} = {{
  .header = {{
    .magic = LV_IMAGE_HEADER_MAGIC,
    .cf = LV_COLOR_FORMAT_A8,
    .flags = 0,
    .w = {size},
    .h = {size},
    .stride = {size},
    .reserved_2 = 0,
  }},
  .data_size = sizeof({symbol}_map),
  .data = {symbol}_map,
  .reserved = NULL,
}};
"""


def convert_job(job: ImageJob) -> None:
    job.source.resolve(strict=True)
    job.outfile.parent.mkdir(parents=True, exist_ok=True)

    pixel_data = render_svg_to_alpha(job.source, job.size)
    c_content = generate_c_file(job, pixel_data)

    job.outfile.write_text(c_content)
    usage = f" ({job.usage})" if job.usage else ""
    print(f"[imagegen] {job.source.name} size={job.size} -> {job.outfile.name}{usage}")


def main() -> None:
    jobs = load_manifest()
    if not jobs:
        print("No image jobs defined.")
        return
    for job in jobs:
        convert_job(job)
    print(f"Images written to {OUT_DIR}")


if __name__ == "__main__":
    main()
