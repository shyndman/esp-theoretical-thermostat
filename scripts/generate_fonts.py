#!/usr/bin/env -S uv run --script
# /// script
# requires-python = "~=3.11"
# dependencies = [
#   "tomli>=2.0.1",
# ]
# ///
"""Generate LVGL font binaries from a TOML manifest."""

import os
import shutil
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, List

import tomli

ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "assets" / "fonts" / "fontgen.toml"
SRC_DIR = ROOT / "assets" / "fonts"
OUT_DIR = ROOT / "main" / "assets" / "fonts"

@dataclass
class FontJob:
    source: Path
    size: int
    lv_name: str
    outfile: Path
    symbols: str
    usage: str


def load_manifest() -> List[FontJob]:
    data = tomli.loads(MANIFEST.read_text())
    jobs: List[FontJob] = []
    for entry in data.get("font", []):
        job = FontJob(
            source=SRC_DIR / entry["source"],
            size=int(entry["size"]),
            lv_name=str(entry["lv_name"]),
            outfile=OUT_DIR / entry["outfile"],
            symbols=str(entry["symbols"]),
            usage=str(entry.get("usage", "")),
        )
        jobs.append(job)
    return jobs


def resolve_font_conv() -> List[str]:
    if shutil.which("lv_font_conv"):
        return ["lv_font_conv"]
    if shutil.which("npx"):
        return ["npx", "--yes", "lv_font_conv"]
    raise SystemExit("lv_font_conv not found; install it or ensure npx is available")


def run_job(cmd_template: List[str], job: FontJob) -> None:
    job.source.resolve(strict=True)
    job.outfile.parent.mkdir(parents=True, exist_ok=True)
    cmd = cmd_template + [
        "--font", str(job.source),
        "--size", str(job.size),
        "--bpp", "4",
        "--format", "lvgl",
        "--lv-include", "lvgl.h",
        "--lv-font-name", job.lv_name,
        "--no-prefilter",
        "--no-compress",
        "--symbols", job.symbols,
        "--output", str(job.outfile),
    ]
    print(f"[lv_font_conv] {job.source.name} size={job.size} -> {job.outfile.name} ({job.usage})")
    subprocess.run(cmd, check=True)


def main() -> None:
    jobs = load_manifest()
    if not jobs:
        print("No font jobs defined.")
        return
    cmd_template = resolve_font_conv()
    for job in jobs:
        run_job(cmd_template, job)
    print(f"Fonts written to {OUT_DIR}")


if __name__ == "__main__":
    main()
