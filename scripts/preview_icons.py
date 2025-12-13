#!/usr/bin/env -S uv run --script
# /// script
# requires-python = "~=3.11"
# dependencies = [
#   "tomli>=2.3.0",
#   "resvg-py>=0.2.4",
# ]
# ///
"""Preview icons from imagegen.toml at 1x and 2x scale using kitty's icat."""

from __future__ import annotations

import subprocess
from dataclasses import dataclass
from pathlib import Path

import resvg_py
import tomli

ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "assets" / "images" / "imagegen.toml"
SRC_DIR = ROOT / "assets" / "images"

BOLD = "\033[1m"
DIM = "\033[2m"
RESET = "\033[0m"


@dataclass
class ImageEntry:
    source: Path
    size: int
    usage: str


def load_manifest() -> list[ImageEntry]:
    if not MANIFEST.exists():
        raise SystemExit(f"Manifest missing: {MANIFEST}")
    data = tomli.loads(MANIFEST.read_text())
    entries: list[ImageEntry] = []
    for entry in data.get("image", []):
        entries.append(
            ImageEntry(
                source=SRC_DIR / entry["source"],
                size=int(entry["size"]),
                usage=str(entry.get("usage", "")),
            )
        )
    return entries


def display_image(path: Path, size: int) -> None:
    """Render SVG at specified size and display with icat."""
    png_bytes = resvg_py.svg_to_bytes(
        svg_path=str(path),
        width=size,
        height=size,
    )
    subprocess.run(
        ["kitten", "icat", "--background", "white", "--stdin=yes"],
        input=bytes(png_bytes),
        check=True,
    )


def preview_entry(entry: ImageEntry) -> None:
    if not entry.source.exists():
        print(f"{BOLD}MISSING:{RESET} {entry.source}")
        return

    rel_path = entry.source.relative_to(ROOT)
    print(f"\n{BOLD}{'━' * 78}{RESET}")
    print(f"{BOLD}{rel_path}{RESET}")
    if entry.usage:
        print(f"{DIM}{entry.usage}{RESET}")
    print(f"{BOLD}{'━' * 78}{RESET}")

    # 1x scale
    print(f"{DIM}{entry.size}px (1x):{RESET}")
    display_image(entry.source, entry.size)
    print()

    # 2x scale
    doubled = entry.size * 2
    print(f"{DIM}{doubled}px (2x):{RESET}")
    display_image(entry.source, doubled)
    print()


def main() -> None:
    entries = load_manifest()
    if not entries:
        print("No images defined in manifest.")
        return

    for entry in entries:
        preview_entry(entry)

    print(f"{BOLD}Done! Previewed {len(entries)} images.{RESET}")


if __name__ == "__main__":
    main()
