#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# dependencies = [
#   "matplotlib>=3.8.0",
# ]
# ///
"""Plot heap usage over time from ESP-IDF logs with event annotations."""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Iterator

import matplotlib.pyplot as plt

PREFIX_RE = re.compile(r"^[A-Z] \((?P<time>\d+)\)")
ANSI_ESCAPE_RE = re.compile(r"\x1b\[[0-9;]*[A-Za-z]")
HEAP_RE = re.compile(
    r"\[heap\](?P<label>[^ ]*) .*?internal free=(?P<int_free>\d+) "
    r"largest=(?P<int_largest>\d+) min=(?P<int_min>\d+) "
    r"dma free=(?P<dma_free>\d+) largest=(?P<dma_largest>\d+) min=(?P<dma_min>\d+)"
)

EVENT_PATTERNS: tuple[tuple[str, str], ...] = (
    ("[boot] Establishing co-processor link… done", "Link Ready"),
    ("[boot] Enabling Wi-Fi… done", "Wi-Fi Ready"),
    ("[boot] Syncing time… done", "Time Sync"),
    ("[boot] Connecting to broker… done", "MQTT Ready"),
    ("[boot] Starting environmental sensors… done", "Sensors Ready"),
    ("[boot] Starting WebRTC publisher…", "WebRTC Start"),
    ("whep_endpoint: Received WHEP offer", "WHEP Offer"),
    ("webrtc_stream: After esp_webrtc_start", "Peer Start"),
    ("DTLS: Server handshake success", "DTLS OK"),
    ("H264_ENC.HW.SET: reference frame allocation failed", "Encoder OOM"),
)


@dataclass
class HeapSample:
    time_ms: int
    label: str
    int_free: int
    int_largest: int
    int_min: int
    dma_free: int
    dma_largest: int
    dma_min: int


@dataclass
class EventMarker:
    time_ms: int
    label: str


def parse_timestamp(line: str) -> int | None:
    match = PREFIX_RE.match(line)
    if not match:
        return None
    return int(match.group("time"))


def parse_heap_line(line: str) -> HeapSample | None:
    ts = parse_timestamp(line)
    if ts is None:
        return None
    info = HEAP_RE.search(line)
    if not info:
        return None
    return HeapSample(
        time_ms=ts,
        label=info.group("label"),
        int_free=int(info.group("int_free")),
        int_largest=int(info.group("int_largest")),
        int_min=int(info.group("int_min")),
        dma_free=int(info.group("dma_free")),
        dma_largest=int(info.group("dma_largest")),
        dma_min=int(info.group("dma_min")),
    )


def detect_events(line: str, seen: set[str]) -> Iterator[EventMarker]:
    ts = parse_timestamp(line)
    if ts is None:
        return
    for needle, label in EVENT_PATTERNS:
        if label in seen:
            continue
        if needle in line:
            seen.add(label)
            yield EventMarker(time_ms=ts, label=label)


def parse_log(lines: Iterable[str]) -> tuple[list[HeapSample], list[EventMarker]]:
    samples: list[HeapSample] = []
    events: list[EventMarker] = []
    seen_events: set[str] = set()
    for line in lines:
        clean = ANSI_ESCAPE_RE.sub("", line)
        heap = parse_heap_line(clean)
        if heap:
            samples.append(heap)
            continue
        events.extend(detect_events(clean, seen_events))
    return samples, events


def plot(samples: list[HeapSample], events: list[EventMarker], output: Path) -> None:
    times = [s.time_ms / 1000 for s in samples]
    fig, ax = plt.subplots(figsize=(20, 10))

    ax.plot(
        times,
        [s.dma_largest for s in samples],
        label="DMA largest block",
        color="tab:blue",
    )
    ax.plot(
        times,
        [s.int_largest for s in samples],
        label="Internal largest block",
        color="tab:green",
    )
    ax.plot(
        times,
        [s.dma_free for s in samples],
        label="DMA free",
        color="tab:red",
        alpha=0.4,
    )

    ymax = (
        max(max(s.dma_free for s in samples), max(s.int_largest for s in samples)) * 1.1
    )
    ax.set_ylim(0, ymax)
    ax.set_xlabel("Time (s since boot)")
    ax.set_ylabel("Bytes")
    ax.set_title("Heap usage trajectory")
    legend = ax.legend(loc="upper right")

    fig.canvas.draw()
    legend_bbox = legend.get_window_extent(fig.canvas.get_renderer())
    legend_height_axes = legend_bbox.transformed(ax.transAxes.inverted()).height
    y_min, y_max = ax.get_ylim()
    data_range = y_max - y_min
    legend_offset = legend_height_axes * data_range
    text_y = y_max - legend_offset - 0.02 * data_range

    for event in events:
        x = event.time_ms / 1000
        ax.axvline(x, color="purple", linestyle="--", linewidth=1, alpha=0.6)
        ax.text(
            x,
            text_y,
            event.label,
            rotation=90,
            color="purple",
            va="top",
            ha="center",
            fontsize=8,
        )

    fig.tight_layout()
    fig.savefig(output)
    print(f"Wrote {output}")


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Plot heap statistics from ESP-IDF logs"
    )
    parser.add_argument(
        "logfile", nargs="?", default="-", help="Input log path or '-' for stdin"
    )
    parser.add_argument(
        "-o", "--output", default="heap_usage.png", help="Output PNG path"
    )
    args = parser.parse_args()

    if args.logfile == "-":
        content = sys.stdin.read().splitlines()
    else:
        log_path = Path(args.logfile)
        if not log_path.exists():
            raise SystemExit(f"Log file not found: {log_path}")
        content = log_path.read_text().splitlines()

    samples, events = parse_log(content)
    if not samples:
        raise SystemExit("No heap samples found in log")

    output_path = Path(args.output)
    plot(samples, events, output_path)


if __name__ == "__main__":
    main()
