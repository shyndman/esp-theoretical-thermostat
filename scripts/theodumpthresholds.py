#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# dependencies = [
#   "paho-mqtt>=2.1.0",
# ]
# ///
"""Send a radar_dump_thresholds command to the configured thermostat."""

from __future__ import annotations

import argparse
import re
import sys
import time
from collections.abc import Mapping
from dataclasses import dataclass
from pathlib import Path

import paho.mqtt.client as mqtt

CONFIG_LINE_RE = re.compile(r"^(CONFIG_[A-Z0-9_]+)=(.*)$")

DEFAULT_MQTT_PORT = 80
DEFAULT_MQTT_PATH = "/"
DEFAULT_THEO_BASE_TOPIC = "theostat"
COMMAND_PAYLOAD = "radar_dump_thresholds"


@dataclass(frozen=True)
class DumpThresholdsConfig:
    host: str
    port: int
    path: str
    topic: str


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Send radar_dump_thresholds command to the thermostat.",
    )
    parser.add_argument("--host", help="MQTT broker host")
    parser.add_argument("--port", type=int, help="MQTT broker port")
    parser.add_argument("--path", help="MQTT WebSocket path")
    parser.add_argument("--topic", help="Explicit MQTT command topic override")
    parser.add_argument("--theo-base", help="Theo base topic override for derived command topic")
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


def normalize_ws_path(path: str | None) -> str:
    value = (path or "").strip()
    if not value or value == "/":
        return "/"
    if not value.startswith("/"):
        return f"/{value}"
    return value


def derive_topic(theo_base: str | None) -> str:
    normalized_base = normalize_topic_base(theo_base) or DEFAULT_THEO_BASE_TOPIC
    return f"{normalized_base}/command"


def resolve_config(args: argparse.Namespace, config_values: Mapping[str, str]) -> DumpThresholdsConfig:
    host = args.host or config_values.get("CONFIG_THEO_MQTT_HOST", "").strip()
    if not host:
        raise SystemExit("MQTT host is required; set CONFIG_THEO_MQTT_HOST or pass --host")

    port = args.port
    if port is None:
        port = int(config_values.get("CONFIG_THEO_MQTT_PORT", str(DEFAULT_MQTT_PORT)))

    path = normalize_ws_path(args.path or config_values.get("CONFIG_THEO_MQTT_PATH") or DEFAULT_MQTT_PATH)

    topic = args.topic or derive_topic(
        args.theo_base or config_values.get("CONFIG_THEO_THEOSTAT_BASE_TOPIC"),
    )

    return DumpThresholdsConfig(host=host, port=port, path=path, topic=topic)


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

    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, transport="websockets")
    client.ws_set_options(path=config.path)

    published = False

    def on_publish(_client, _userdata, _mid, _reason_code, _properties):
        nonlocal published
        published = True

    client.on_publish = on_publish

    try:
        print(f"Connecting to ws://{config.host}:{config.port}{config.path}...", file=sys.stderr)
        client.connect(config.host, config.port, keepalive=60)
        client.loop_start()

        print(f"Publishing {COMMAND_PAYLOAD!r} to {config.topic}...", file=sys.stderr)
        client.publish(config.topic, COMMAND_PAYLOAD, qos=0)

        start_time = time.time()
        while not published and time.time() - start_time < 5:
            time.sleep(0.1)

        if published:
            print("Command sent successfully.", file=sys.stderr)
        else:
            print("Timed out waiting for command to be published.", file=sys.stderr)

        client.loop_stop()
    except Exception as exc:
        print(f"theodumpthresholds: {exc}", file=sys.stderr)
        return 1
    finally:
        client.disconnect()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
