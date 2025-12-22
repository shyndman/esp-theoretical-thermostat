#!/usr/bin/env -S uv run --script
# /// script
# requires-python = "~=3.11"
# dependencies = [
#   "tomli>=2.0.1",
#   "pydub>=0.25.1",
#   "audioop-lts>=0.2.1",
# ]
# ///
"""Convert declarative sound entries into C arrays for firmware embedding."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import tomli
from pydub import AudioSegment

ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "assets" / "audio" / "soundgen.toml"
SRC_DIR = ROOT / "assets" / "audio"
OUT_DIR = ROOT / "main" / "assets" / "audio"


def require_manifest() -> None:
    if not MANIFEST.exists():
        raise SystemExit(f"Manifest missing: {MANIFEST}")


@dataclass
class SoundJob:
    source: Path
    outfile: Path
    symbol: str
    sample_rate: int
    channels: int
    bits_per_sample: int
    usage: str


def load_manifest() -> list[SoundJob]:
    data = tomli.loads(MANIFEST.read_text())
    jobs: list[SoundJob] = []
    for entry in data.get("sound", []):
        symbol = entry.get("symbol")
        if not symbol:
            raise SystemExit("Each sound entry must declare a 'symbol'")
        job = SoundJob(
            source=SRC_DIR / entry["source"],
            outfile=OUT_DIR / entry["outfile"],
            symbol=str(symbol),
            sample_rate=int(entry.get("sample_rate", 16000)),
            channels=int(entry.get("channels", 1)),
            bits_per_sample=int(entry.get("bits_per_sample", 16)),
            usage=str(entry.get("usage", "")),
        )
        jobs.append(job)
    return jobs


def normalize_audio(source: Path, target_rate: int, target_channels: int, target_bits: int) -> bytes:
    """Load audio file and convert to target format, returning raw PCM bytes."""
    audio = AudioSegment.from_file(source)

    if audio.frame_rate != target_rate:
        audio = audio.set_frame_rate(target_rate)
    if audio.channels != target_channels:
        audio = audio.set_channels(target_channels)
    if audio.sample_width != target_bits // 8:
        audio = audio.set_sample_width(target_bits // 8)

    return audio.raw_data


def convert_job(job: SoundJob) -> None:
    job.source.resolve(strict=True)
    job.outfile.parent.mkdir(parents=True, exist_ok=True)

    frames = normalize_audio(
        job.source, job.sample_rate, job.channels, job.bits_per_sample
    )

    array_body = format_bytes(frames)
    rel_src = job.source.relative_to(ROOT)
    lines = [
        f"// Auto-generated from {rel_src}",
        f"// Sample rate: {job.sample_rate} Hz, Channels: {job.channels}, Bits: {job.bits_per_sample}",
        "",
        "#include <stddef.h>",
        "#include <stdint.h>",
        "",
        f"const uint8_t {job.symbol}[] = {{",
    ]
    if array_body:
        lines.append(array_body)
    lines.append("};")
    lines.append("")
    lines.append(f"const size_t {job.symbol}_len = sizeof({job.symbol});")
    lines.append("")
    contents = "\n".join(lines)

    job.outfile.write_text(contents)
    usage = f" ({job.usage})" if job.usage else ""
    print(f"[soundgen] {job.source.name} -> {job.outfile.relative_to(ROOT)}{usage}")


def format_bytes(data: bytes) -> str:
    if not data:
        return ""
    lines: list[str] = []
    for idx in range(0, len(data), 12):
        chunk = data[idx : idx + 12]
        line = ", ".join(f"0x{b:02x}" for b in chunk)
        lines.append("    " + line + ",")
    return "\n".join(lines)


def main() -> None:
    require_manifest()
    jobs = load_manifest()
    if not jobs:
        print("No sound jobs defined.")
        return
    for job in jobs:
        convert_job(job)
    print(f"Sounds written to {OUT_DIR}")


if __name__ == "__main__":
    main()
