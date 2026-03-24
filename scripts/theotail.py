#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# dependencies = [
#   "paho-mqtt>=2.1.0",
# ]
# ///
"""Tail thermostat MQTT log messages over WebSockets."""

from __future__ import annotations

import argparse
import re
import sys
from collections.abc import Mapping
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path

import paho.mqtt.client as mqtt

CONFIG_LINE_RE = re.compile(r"^(CONFIG_[A-Z0-9_]+)=(.*)$")

DEFAULT_MQTT_PORT = 80
DEFAULT_MQTT_PATH = "/"
DEFAULT_THEO_BASE_TOPIC = "theostat"
DEFAULT_DEVICE_SLUG = "hallway"


@dataclass(frozen=True)
class TailConfig:
    host: str
    port: int
    path: str
    topic: str


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Tail thermostat MQTT logs from the configured broker.",
    )
    parser.add_argument("--host", help="MQTT broker host")
    parser.add_argument("--port", type=int, help="MQTT broker port")
    parser.add_argument("--path", help="MQTT WebSocket path")
    parser.add_argument("--topic", help="Explicit MQTT topic to subscribe to")
    parser.add_argument("--theo-base", help="Theo base topic override for derived log topic")
    parser.add_argument("--slug", help="Device slug override for derived log topic")
    parser.add_argument(
        "--timestamp",
        action="store_true",
        help="Prefix each received log line with the local receive time",
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
        value = value[1:-1]
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
    return f"{normalized_base}/{normalized_slug}/logs"


def resolve_config(args: argparse.Namespace, config_values: Mapping[str, str]) -> TailConfig:
    host = args.host or config_values.get("CONFIG_THEO_MQTT_HOST", "").strip()
    if not host:
        raise SystemExit("MQTT host is required; set CONFIG_THEO_MQTT_HOST or pass --host")

    port = args.port
    if port is None:
        port = int(config_values.get("CONFIG_THEO_MQTT_PORT", str(DEFAULT_MQTT_PORT)))

    path = normalize_ws_path(args.path or config_values.get("CONFIG_THEO_MQTT_PATH") or DEFAULT_MQTT_PATH)

    topic = args.topic or derive_topic(
        args.theo_base or config_values.get("CONFIG_THEO_THEOSTAT_BASE_TOPIC"),
        args.slug or config_values.get("CONFIG_THEO_DEVICE_SLUG"),
    )

    return TailConfig(host=host, port=port, path=path, topic=topic)


def format_payload(payload: bytes, include_timestamp: bool) -> str:
    text = payload.decode("utf-8", errors="replace").rstrip("\r\n")
    single_line = text.replace("\r", r"\r").replace("\n", r"\n")
    if not include_timestamp:
        return single_line
    timestamp = datetime.now().astimezone().strftime("%Y-%m-%d %H:%M:%S")
    return f"[{timestamp}] {single_line}"


def build_client(config: TailConfig, include_timestamp: bool) -> mqtt.Client:
    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, transport="websockets")
    client.ws_set_options(path=config.path)

    def on_connect(
        client: mqtt.Client,
        _userdata: object,
        _connect_flags: mqtt.ConnectFlags,
        reason_code: mqtt.ReasonCode,
        _properties: mqtt.Properties | None,
    ) -> None:
        if reason_code.is_failure:
            print(f"theotail: connect failed ({reason_code})", file=sys.stderr)
            return
        client.subscribe(config.topic)
        print(
            f"theotail: subscribed to {config.topic} via ws://{config.host}:{config.port}{config.path}",
            file=sys.stderr,
        )

    def on_disconnect(
        _client: mqtt.Client,
        _userdata: object,
        _disconnect_flags: mqtt.DisconnectFlags,
        reason_code: mqtt.ReasonCode,
        _properties: mqtt.Properties | None,
    ) -> None:
        if reason_code != 0:
            print(f"theotail: disconnected ({reason_code})", file=sys.stderr)

    def on_message(
        _client: mqtt.Client,
        _userdata: object,
        message: mqtt.MQTTMessage,
    ) -> None:
        print(format_payload(message.payload, include_timestamp), flush=True)

    client.on_connect = on_connect
    client.on_disconnect = on_disconnect
    client.on_message = on_message
    return client


def main() -> int:
    args = parse_args()
    repo_root = Path(__file__).resolve().parent.parent
    config_values = load_kconfig_values(
        [
            repo_root / "sdkconfig.defaults",
            repo_root / "sdkconfig.defaults.local",
            repo_root / "sdkconfig",
        ]
    )
    config = resolve_config(args, config_values)
    client = build_client(config, args.timestamp)

    try:
        client.connect(config.host, config.port, keepalive=60)
        client.loop_forever()
    except KeyboardInterrupt:
        return 0
    except Exception as exc:
        print(f"theotail: {exc}", file=sys.stderr)
        return 1
    finally:
        client.disconnect()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
