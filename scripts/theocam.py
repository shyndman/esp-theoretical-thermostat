#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# dependencies = [
#   "paho-mqtt>=2.1.0",
# ]
# ///
"""Show the latest thermostat camera snapshot in Kitty."""

from __future__ import annotations

import argparse
import re
import shutil
import subprocess
import sys
import time
from collections.abc import Mapping
from dataclasses import dataclass
from pathlib import Path

import paho.mqtt.client as mqtt

CONFIG_LINE_RE = re.compile(r"^(CONFIG_[A-Z0-9_]+)=(.*)$")
JPEG_MAGIC = b"\xff\xd8"

DEFAULT_MQTT_PORT = 80
DEFAULT_MQTT_PATH = "/"
DEFAULT_THEO_BASE_TOPIC = "theostat"
DEFAULT_DEVICE_SLUG = "hallway"
DEFAULT_TIMEOUT_SECONDS = 15.0


@dataclass(frozen=True)
class CamConfig:
    host: str
    port: int
    path: str
    topic: str
    timeout_seconds: float


class ConfigError(ValueError):
    pass


class SnapshotError(Exception):
    pass


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Subscribe to the configured thermostat camera snapshot topic and display one JPEG with Kitty icat.",
    )
    parser.add_argument("--host", help="MQTT broker host")
    parser.add_argument("--port", type=int, help="MQTT broker port")
    parser.add_argument("--path", help="MQTT WebSocket path")
    parser.add_argument("--topic", help="Explicit MQTT topic to subscribe to")
    parser.add_argument("--theo-base", help="Theo base topic override for derived snapshot topic")
    parser.add_argument("--slug", help="Device slug override for derived snapshot topic")
    parser.add_argument(
        "--timeout",
        type=float,
        default=DEFAULT_TIMEOUT_SECONDS,
        help="Seconds to wait for a snapshot before exiting",
    )
    return parser.parse_args()


def load_kconfig_values(files: list[Path]) -> dict[str, str]:
    values: dict[str, str] = {}
    for path in files:
        if not path.exists():
            continue
        for raw_line in path.read_text(encoding="utf-8").splitlines():
            line = raw_line.strip()
            if not line or line.startswith("#"):
                continue
            match = CONFIG_LINE_RE.match(line)
            if not match:
                continue
            key, raw_value = match.groups()
            values[key] = parse_kconfig_value(raw_value)
    return values


def parse_kconfig_value(raw_value: str) -> str:
    value = raw_value.strip()
    if len(value) >= 2 and value.startswith('"') and value.endswith('"'):
        return value[1:-1]
    return value


def normalize_topic_base(topic: str | None) -> str:
    if topic is None:
        return ""
    return topic.strip().strip("/")


def normalize_slug(slug: str | None) -> str:
    if not slug:
        return ""

    normalized: list[str] = []
    prev_was_dash = True
    for char in slug.lower():
        if char.isalnum():
            normalized.append(char)
            prev_was_dash = False
        elif not prev_was_dash:
            normalized.append("-")
            prev_was_dash = True

    while normalized and normalized[-1] == "-":
        normalized.pop()

    return "".join(normalized)


def normalize_ws_path(path: str | None) -> str:
    value = (path or "").strip()
    if not value or value == "/":
        return "/"
    if not value.startswith("/"):
        return f"/{value}"
    return value


def derive_topic(theo_base: str | None, slug: str | None) -> str:
    normalized_base = normalize_topic_base(theo_base) or DEFAULT_THEO_BASE_TOPIC
    normalized_slug = normalize_slug(slug) or DEFAULT_DEVICE_SLUG
    return f"{normalized_base}/{normalized_slug}/camera/snapshot"


def resolve_config(args: argparse.Namespace, values: Mapping[str, str]) -> CamConfig:
    host = args.host or values.get("CONFIG_THEO_MQTT_HOST", "").strip()
    if not host:
        raise ConfigError("MQTT host is required; set CONFIG_THEO_MQTT_HOST or pass --host")

    port = args.port
    if port is None:
        port = int(values.get("CONFIG_THEO_MQTT_PORT", str(DEFAULT_MQTT_PORT)))

    path = normalize_ws_path(args.path or values.get("CONFIG_THEO_MQTT_PATH") or DEFAULT_MQTT_PATH)
    topic = args.topic or derive_topic(
        args.theo_base or values.get("CONFIG_THEO_THEOSTAT_BASE_TOPIC"),
        args.slug or values.get("CONFIG_THEO_DEVICE_SLUG"),
    )

    if args.timeout <= 0:
        raise ConfigError("--timeout must be greater than 0")

    return CamConfig(
        host=host,
        port=port,
        path=path,
        topic=topic,
        timeout_seconds=args.timeout,
    )


def receive_snapshot(config: CamConfig) -> bytes:
    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, transport="websockets")
    client.ws_set_options(path=config.path)
    snapshot: bytes | None = None
    connected = False
    failed_reason: str | None = None

    def on_connect(
        client: mqtt.Client,
        _userdata: object,
        _connect_flags: mqtt.ConnectFlags,
        reason_code: mqtt.ReasonCode,
        _properties: mqtt.Properties | None,
    ) -> None:
        nonlocal connected, failed_reason
        if reason_code.is_failure:
            failed_reason = f"connect failed ({reason_code})"
            return
        connected = True
        client.subscribe(config.topic)
        print(
            f"theocam: subscribed to {config.topic} via ws://{config.host}:{config.port}{config.path}",
            file=sys.stderr,
        )

    def on_disconnect(
        _client: mqtt.Client,
        _userdata: object,
        _disconnect_flags: mqtt.DisconnectFlags,
        reason_code: mqtt.ReasonCode,
        _properties: mqtt.Properties | None,
    ) -> None:
        nonlocal failed_reason
        if snapshot is None and reason_code != 0:
            failed_reason = f"disconnected ({reason_code})"

    def on_message(
        client: mqtt.Client,
        _userdata: object,
        message: mqtt.MQTTMessage,
    ) -> None:
        nonlocal failed_reason, snapshot
        if not message.payload.startswith(JPEG_MAGIC):
            failed_reason = f"received non-JPEG payload on {message.topic} ({len(message.payload)} bytes)"
            print(f"theocam: {failed_reason}", file=sys.stderr)
            return
        snapshot = bytes(message.payload)
        client.disconnect()

    client.on_connect = on_connect
    client.on_disconnect = on_disconnect
    client.on_message = on_message

    try:
        client.connect(config.host, config.port, keepalive=60)
        client.loop_start()

        deadline = time.monotonic() + config.timeout_seconds
        while snapshot is None and failed_reason is None and time.monotonic() < deadline:
            time.sleep(0.05)
    finally:
        client.loop_stop()
        client.disconnect()

    if snapshot is not None:
        return snapshot
    if failed_reason is not None:
        raise SnapshotError(failed_reason)
    if not connected:
        raise SnapshotError("timed out before MQTT connection completed")
    raise SnapshotError(f"timed out waiting for JPEG on {config.topic}")


def show_with_kitty(snapshot: bytes) -> None:
    kitten = shutil.which("kitten")
    if kitten is None:
        raise SnapshotError("kitten executable not found; Kitty icat is required")

    subprocess.run(
        [kitten, "icat", "--stdin", "yes"],
        input=snapshot,
        check=True,
    )


def main() -> int:
    repo_root = Path(__file__).resolve().parent.parent
    args = parse_args()
    values = load_kconfig_values(
        [
            repo_root / "sdkconfig.defaults",
            repo_root / "sdkconfig.defaults.local",
            repo_root / "sdkconfig",
        ]
    )

    try:
        config = resolve_config(args, values)
        snapshot = receive_snapshot(config)
        print(f"theocam: received {len(snapshot)} bytes", file=sys.stderr)
        show_with_kitty(snapshot)
    except (ConfigError, SnapshotError, OSError, subprocess.CalledProcessError) as exc:
        print(f"theocam: {exc}", file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
