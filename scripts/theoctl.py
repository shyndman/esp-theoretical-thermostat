#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# dependencies = [
#   "paho-mqtt>=2.1.0",
# ]
# ///
"""Send thermostat commands to the configured MQTT command topic."""

from __future__ import annotations

import argparse
import re
import sys
import time
from collections.abc import Mapping, Sequence
from dataclasses import dataclass
from pathlib import Path

import paho.mqtt.client as mqtt

CONFIG_LINE_RE = re.compile(r"^(CONFIG_[A-Z0-9_]+)=(.*)$")

DEFAULT_MQTT_PORT = 80
DEFAULT_MQTT_PATH = "/"
DEFAULT_THEO_BASE_TOPIC = "theostat"
SUPPORTED_COMMANDS: tuple[str, ...] = (
    "coolwave",
    "heatwave",
    "radar_calibrate",
    "radar_dump_thresholds",
    "rainbow",
    "restart",
    "sparkle",
)


@dataclass(frozen=True)
class TheoctlConfig:
    host: str
    port: int
    path: str
    topic: str


def parse_args(argv: Sequence[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Send a supported command to the thermostat.",
    )
    parser.add_argument(
        "command",
        nargs="?",
        choices=SUPPORTED_COMMANDS,
        help="Command to publish",
    )
    parser.add_argument(
        "--list-commands",
        action="store_true",
        help="List supported commands and exit",
    )
    args = parser.parse_args(argv)
    if not args.list_commands and args.command is None:
        parser.error("the following arguments are required: command")
    return args


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


def resolve_config(config_values: Mapping[str, str]) -> TheoctlConfig:
    host = config_values.get("CONFIG_THEO_MQTT_HOST", "").strip()
    if not host:
        raise SystemExit("theoctl: MQTT host is required; set CONFIG_THEO_MQTT_HOST in repo config")

    port = int(config_values.get("CONFIG_THEO_MQTT_PORT", str(DEFAULT_MQTT_PORT)))
    path = normalize_ws_path(config_values.get("CONFIG_THEO_MQTT_PATH") or DEFAULT_MQTT_PATH)
    topic = derive_topic(config_values.get("CONFIG_THEO_THEOSTAT_BASE_TOPIC"))

    return TheoctlConfig(host=host, port=port, path=path, topic=topic)


def list_commands() -> None:
    for command in SUPPORTED_COMMANDS:
        print(command)


def main() -> int:
    args = parse_args()
    if args.list_commands:
        list_commands()
        return 0

    repo_root = Path(__file__).resolve().parent.parent
    config_values = load_kconfig_values(
        [
            repo_root / "sdkconfig.defaults",
            repo_root / "sdkconfig.defaults.local",
            repo_root / "sdkconfig",
        ]
    )
    config = resolve_config(config_values)

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

        print(f"Publishing {args.command!r} to {config.topic}...", file=sys.stderr)
        client.publish(config.topic, args.command, qos=0)

        start_time = time.time()
        while not published and time.time() - start_time < 5:
            time.sleep(0.1)

        if published:
            print("Command sent successfully.", file=sys.stderr)
        else:
            print("Timed out waiting for command to be published.", file=sys.stderr)

        client.loop_stop()
    except Exception as exc:
        print(f"theoctl: {exc}", file=sys.stderr)
        return 1
    finally:
        client.disconnect()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
