#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# ///
"""Serve a local same-origin WebRTC probe page for the configured thermostat."""

from __future__ import annotations

import argparse
import html
import json
import re
import sys
from collections.abc import Mapping
from dataclasses import dataclass
from datetime import datetime
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.error import HTTPError, URLError
from urllib.parse import parse_qsl, urlencode, urlsplit, urlunsplit
from urllib.request import Request, urlopen

CONFIG_LINE_RE = re.compile(r"^(CONFIG_[A-Z0-9_]+)=(.*)$")
DEFAULT_LISTEN_HOST = "127.0.0.1"
DEFAULT_LISTEN_PORT = 8765
DEFAULT_WEBRTC_PATH = "/api/webrtc"
PROBE_PAGE_TITLE = "Theo WebRTC Probe"


@dataclass(frozen=True)
class ProbeConfig:
    listen_host: str
    listen_port: int
    thermostat_host: str
    thermostat_port: int
    proxy_path: str
    thermostat_target_url: str


class ConfigError(ValueError):
    pass


class ProxyError(Exception):
    pass


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Serve a local same-origin browser probe for the configured thermostat WHEP endpoint.",
    )
    parser.add_argument("--listen-host", default=DEFAULT_LISTEN_HOST, help="Local host to bind")
    parser.add_argument("--listen-port", type=int, default=DEFAULT_LISTEN_PORT, help="Local port to bind")
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


def require_config_value(values: Mapping[str, str], key: str, message: str) -> str:
    value = values.get(key, "").strip()
    if not value:
        raise ConfigError(message)
    return value


def normalize_http_path(raw_path: str | None) -> str:
    value = (raw_path or "").strip()
    if not value:
        return DEFAULT_WEBRTC_PATH

    split = urlsplit(value if value.startswith("/") else f"/{value}")
    path = split.path or "/"
    query = urlencode(parse_qsl(split.query, keep_blank_values=True), doseq=True)
    return urlunsplit(("", "", path, query, ""))


def append_stream_id(path: str, stream_id: str | None) -> str:
    split = urlsplit(path)
    query_items = parse_qsl(split.query, keep_blank_values=True)
    if stream_id:
        query_items = [(key, value) for key, value in query_items if key != "src"]
        query_items.append(("src", stream_id))
    query = urlencode(query_items, doseq=True)
    return urlunsplit(("", "", split.path or "/", query, ""))


def resolve_config(args: argparse.Namespace, values: Mapping[str, str]) -> ProbeConfig:
    thermostat_host = require_config_value(
        values,
        "CONFIG_THEO_WIFI_STA_STATIC_IP",
        "Missing CONFIG_THEO_WIFI_STA_STATIC_IP in sdkconfig.defaults/local/sdkconfig",
    )
    thermostat_port = int(
        require_config_value(
            values,
            "CONFIG_THEO_OTA_PORT",
            "Missing CONFIG_THEO_OTA_PORT in sdkconfig.defaults/local/sdkconfig",
        )
    )
    proxy_path = append_stream_id(
        normalize_http_path(values.get("CONFIG_THEO_WEBRTC_PATH")),
        values.get("CONFIG_THEO_WEBRTC_STREAM_ID", "").strip() or None,
    )
    thermostat_target_url = f"http://{thermostat_host}:{thermostat_port}{proxy_path}"
    return ProbeConfig(
        listen_host=args.listen_host,
        listen_port=args.listen_port,
        thermostat_host=thermostat_host,
        thermostat_port=thermostat_port,
        proxy_path=proxy_path,
        thermostat_target_url=thermostat_target_url,
    )


def render_page(config: ProbeConfig) -> bytes:
    page = f"""<!doctype html>
<html lang=\"en\">
<head>
  <meta charset=\"utf-8\">
  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">
  <title>{html.escape(PROBE_PAGE_TITLE)}</title>
  <style>
    :root {{
      color-scheme: dark;
      --bg: #0b1020;
      --panel: #121a33;
      --text: #eef2ff;
      --muted: #a5b4fc;
      --browser-bg: #172554;
      --helper-bg: #3f1d2e;
      --thermostat-bg: #1f4d3b;
      --system-bg: #3a2f0f;
      --error: #dc2626;
    }}
    * {{ box-sizing: border-box; }}
    body {{
      margin: 0;
      min-height: 100vh;
      background: var(--bg);
      color: var(--text);
      font: 14px/1.5 ui-monospace, SFMono-Regular, Menlo, Consolas, monospace;
    }}
    main {{
      max-width: 1200px;
      margin: 0 auto;
      padding: 24px;
    }}
    h1 {{ margin: 0 0 8px; font-size: 24px; }}
    p {{ margin: 0; color: var(--muted); }}
    .controls {{ margin: 20px 0; display: flex; gap: 12px; flex-wrap: wrap; align-items: center; }}
    button {{
      border: 0;
      border-radius: 999px;
      padding: 10px 16px;
      background: #4f46e5;
      color: white;
      font: inherit;
      cursor: pointer;
    }}
    button:disabled {{ opacity: 0.6; cursor: wait; }}
    label {{
      display: flex;
      gap: 8px;
      align-items: center;
      padding: 10px 14px;
      border-radius: 999px;
      background: var(--panel);
    }}
    input {{
      width: 160px;
      border: 0;
      border-radius: 999px;
      padding: 6px 10px;
      background: #0f172a;
      color: var(--text);
      font: inherit;
    }}
    .meta {{
      padding: 12px 14px;
      border-radius: 12px;
      background: var(--panel);
      margin-bottom: 20px;
      white-space: pre-wrap;
      word-break: break-word;
    }}
    video {{
      width: 100%;
      max-height: 420px;
      border-radius: 12px;
      background: black;
      margin-bottom: 20px;
    }}
    #console {{ display: grid; gap: 10px; }}
    .entry {{
      display: grid;
      grid-template-columns: auto 1fr;
      gap: 10px;
      align-items: start;
      padding: 12px 14px;
      border-radius: 12px;
      background: var(--panel);
      white-space: pre-wrap;
      word-break: break-word;
    }}
    .entry.browser {{ background: var(--browser-bg); }}
    .entry.helper {{ background: var(--helper-bg); }}
    .entry.thermostat {{ background: var(--thermostat-bg); }}
    .entry.system {{ background: var(--system-bg); }}
    .time {{ color: #c7d2fe; }}
    .entry-body {{ min-width: 0; }}
    .pill-error {{
      display: inline-block;
      margin: 0 0 8px;
      padding: 2px 8px;
      border-radius: 999px;
      background: var(--error);
      color: white;
      font-weight: 700;
    }}
    pre {{ margin: 0; white-space: pre-wrap; word-break: break-word; }}
  </style>
</head>
<body>
  <main>
    <h1>{html.escape(PROBE_PAGE_TITLE)}</h1>
    <p>Same-origin local helper for the configured thermostat WHEP endpoint.</p>
    <div class=\"controls\">
      <label>
        Browser IP
        <input id=\"browserIp\" type=\"text\" inputmode=\"decimal\" placeholder=\"192.168.1.10\" spellcheck=\"false\">
      </label>
      <button id=\"start\">Start probe</button>
    </div>
    <div class=\"meta\" id=\"meta\"></div>
    <video id=\"remoteVideo\" autoplay playsinline controls muted></video>
    <div id=\"console\" aria-live=\"polite\"></div>
  </main>
  <script>
    const proxyPath = {json.dumps(config.proxy_path)};
    const thermostatTarget = {json.dumps(config.thermostat_target_url)};
    const defaultBrowserIp = '192.168.86.22';
    const consoleEl = document.getElementById('console');
    const buttonEl = document.getElementById('start');
    const browserIpEl = document.getElementById('browserIp');
    const metaEl = document.getElementById('meta');
    const videoEl = document.getElementById('remoteVideo');
    const browserIpStorageKey = 'theoprobe.browserIp';
    let sessionStart = null;
    let sessionStartAbsolute = null;
    let currentPc = null;
    let statsTimer = null;

    function getBrowserIpOverride() {{
      return browserIpEl.value.trim();
    }}

    function renderMeta() {{
      metaEl.textContent = [
        `Local page: ${{window.location.origin}}/`,
        `Local proxy: ${{window.location.origin}}${{proxyPath}}`,
        `Thermostat target: ${{thermostatTarget}}`,
        `Browser IP override: ${{getBrowserIpOverride() || 'off'}}`,
        `Reference event: ${{sessionStartAbsolute || 'not started'}}`,
        'Backgrounds: browser=blue thermostat=green helper=magenta system=amber',
      ].join('\\n');
    }}
    browserIpEl.value = window.localStorage.getItem(browserIpStorageKey) || defaultBrowserIp;
    renderMeta();

    browserIpEl.addEventListener('input', () => {{
      window.localStorage.setItem(browserIpStorageKey, getBrowserIpOverride());
      renderMeta();
    }});

    function formatElapsed() {{
      const zero = sessionStart ?? performance.now();
      const totalMs = performance.now() - zero;
      const minutes = Math.floor(totalMs / 60000);
      const seconds = Math.floor((totalMs % 60000) / 1000);
      const hundredths = Math.floor((totalMs % 1000) / 10);
      return `${{String(minutes).padStart(2, '0')}}:${{String(seconds).padStart(2, '0')}}.${{String(hundredths).padStart(2, '0')}}`;
    }}

    function appendLog(source, message, isError = false) {{
      const entry = document.createElement('div');
      entry.className = `entry ${{source}}`;
      entry.title = source;

      const time = document.createElement('span');
      time.className = 'time';
      time.textContent = formatElapsed();

      const body = document.createElement('div');
      body.className = 'entry-body';
      if (isError) {{
        const pill = document.createElement('span');
        pill.className = 'pill-error';
        pill.textContent = 'ERROR';
        body.appendChild(pill);
      }}

      const text = document.createElement('pre');
      text.textContent = message;
      body.appendChild(text);

      entry.append(time, body);
      consoleEl.appendChild(entry);
      entry.scrollIntoView({{ block: 'end' }});
    }}

    async function logStats(pc) {{
      const report = await pc.getStats();
      const raw = JSON.stringify(Array.from(report.values()), null, 2);
      appendLog('browser', raw);
    }}

    function stopStats() {{
      if (statsTimer) {{
        clearInterval(statsTimer);
        statsTimer = null;
      }}
    }}

    function startStats(pc) {{
      stopStats();
      statsTimer = window.setInterval(() => {{
        if (!pc || pc.connectionState === 'closed') {{
          stopStats();
          return;
        }}
        void logStats(pc).catch((error) => appendLog('browser', String(error), true));
      }}, 1000);
    }}

    function waitForIceGatheringComplete(pc) {{
      if (pc.iceGatheringState === 'complete') {{
        return Promise.resolve();
      }}
      return new Promise((resolve) => {{
        const onState = () => {{
          if (pc.iceGatheringState === 'complete') {{
            pc.removeEventListener('icegatheringstatechange', onState);
            resolve();
          }}
        }};
        pc.addEventListener('icegatheringstatechange', onState);
      }});
    }}

    function rewriteBrowserHostCandidates(rawOffer, browserIp) {{
      if (!browserIp) {{
        return {{ sdp: rawOffer, rewrittenCandidates: 0, droppedTcpCandidates: 0 }};
      }}

      let rewrittenCandidates = 0;
      let droppedTcpCandidates = 0;
      const lines = rawOffer.split('\\r\\n');
      const rewrittenLines = [];

      for (const line of lines) {{
        if (line.startsWith('a=candidate:') && line.includes(' typ host')) {{
          if (/\\s(?:tcp|TCP)\\s/.test(line)) {{
            droppedTcpCandidates += 1;
            continue;
          }}
          if (line.includes('.local')) {{
            const rewrittenLine = line.replace(
              /^(a=candidate:\\S+ \\d+ (?:udp|UDP|tcp|TCP) \\d+ )([^\\s]+)( \\d+ typ host.*)$/,
              `$1${{browserIp}}$3`,
            );
            if (rewrittenLine !== line) {{
              rewrittenCandidates += 1;
              rewrittenLines.push(rewrittenLine);
              continue;
            }}
          }}
        }}

        if (line.startsWith('a=rtcp:') && line.includes('.local')) {{
          rewrittenLines.push(line.replace(/(a=rtcp:\\d+ IN IP4 )([^\\s]+)/, `$1${{browserIp}}`));
          continue;
        }}

        rewrittenLines.push(line);
      }}

      return {{
        sdp: rewrittenLines.join('\\r\\n'),
        rewrittenCandidates,
        droppedTcpCandidates,
      }};
    }}

    function wirePeerConnectionLogs(pc) {{
      const logState = (name, value) => appendLog('browser', `${{name}}: ${{value}}`);
      pc.addEventListener('signalingstatechange', () => logState('signalingState', pc.signalingState));
      pc.addEventListener('icegatheringstatechange', () => logState('iceGatheringState', pc.iceGatheringState));
      pc.addEventListener('iceconnectionstatechange', () => logState('iceConnectionState', pc.iceConnectionState));
      pc.addEventListener('connectionstatechange', () => logState('connectionState', pc.connectionState));
      pc.addEventListener('track', (event) => {{
        if (event.streams[0]) {{
          videoEl.srcObject = event.streams[0];
        }}
        appendLog('browser', `track: kind=${{event.track.kind}} id=${{event.track.id}} streams=${{event.streams.length}}`);
      }});
      pc.addEventListener('icecandidate', (event) => {{
        if (event.candidate) {{
          appendLog('browser', event.candidate.candidate);
          return;
        }}
        appendLog('browser', 'icecandidate: complete');
      }});
      pc.addEventListener('icecandidateerror', (event) => appendLog('browser', `${{event.errorText || 'icecandidateerror'}}`, true));
    }}

    async function startProbe() {{
      buttonEl.disabled = true;
      stopStats();
      if (currentPc) {{
        currentPc.close();
      }}
      videoEl.srcObject = null;
      consoleEl.textContent = '';
      sessionStart = performance.now();
      sessionStartAbsolute = new Date().toISOString();
      renderMeta();

      const pc = new RTCPeerConnection();
      currentPc = pc;
      wirePeerConnectionLogs(pc);
      pc.addTransceiver('audio', {{ direction: 'recvonly' }});
      pc.addTransceiver('video', {{ direction: 'recvonly' }});

      try {{
        appendLog('system', `probe.start\n${{sessionStartAbsolute}}`);
        const offer = await pc.createOffer();
        await pc.setLocalDescription(offer);
        await waitForIceGatheringComplete(pc);

        const rawOffer = pc.localDescription?.sdp || '';
        appendLog('browser', rawOffer);

        const browserIp = getBrowserIpOverride();
        const rewrittenOffer = rewriteBrowserHostCandidates(rawOffer, browserIp);
        if (rewrittenOffer.rewrittenCandidates > 0 || rewrittenOffer.droppedTcpCandidates > 0) {{
          appendLog(
            'helper',
            `rewrote_offer browser_ip=${{browserIp}} rewritten_host_candidates=${{rewrittenOffer.rewrittenCandidates}} dropped_tcp_candidates=${{rewrittenOffer.droppedTcpCandidates}}`,
          );
          appendLog('helper', rewrittenOffer.sdp);
        }}

        const response = await fetch(proxyPath, {{
          method: 'POST',
          headers: {{ 'Content-Type': 'application/sdp' }},
          body: rewrittenOffer.sdp,
        }});
        const rawAnswer = await response.text();
        appendLog('browser', `response: ${{response.status}} ${{response.statusText}}`);

        if (!response.ok) {{
          appendLog(response.status === 502 ? 'helper' : 'thermostat', rawAnswer, true);
          return;
        }}

        appendLog('thermostat', rawAnswer);
        await pc.setRemoteDescription({{ type: 'answer', sdp: rawAnswer }});
        appendLog('browser', 'remoteDescription: answer applied');
        await logStats(pc);
        startStats(pc);
      }} catch (error) {{
        appendLog('browser', String(error), true);
      }} finally {{
        buttonEl.disabled = false;
      }}
    }}

    videoEl.addEventListener('loadedmetadata', () => appendLog('browser', `video.loadedmetadata: ${{videoEl.videoWidth}}x${{videoEl.videoHeight}}`));
    videoEl.addEventListener('playing', () => appendLog('browser', 'video.playing'));
    videoEl.addEventListener('resize', () => appendLog('browser', `video.resize: ${{videoEl.videoWidth}}x${{videoEl.videoHeight}}`));
    videoEl.addEventListener('error', () => appendLog('browser', 'video.error', true));

    buttonEl.addEventListener('click', () => {{
      void startProbe();
    }});
  </script>
</body>
</html>
"""
    return page.encode("utf-8")


class ProbeRequestHandler(BaseHTTPRequestHandler):
    server: "ProbeServer"

    def do_GET(self) -> None:  # noqa: N802
        self.log_access("->")
        if self.path not in {"/", "/index.html"}:
            self.send_error(HTTPStatus.NOT_FOUND)
            return

        body = render_page(self.server.probe_config)
        self.send_response(HTTPStatus.OK)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_POST(self) -> None:  # noqa: N802
        self.log_access("->")
        if self.path != self.server.probe_config.proxy_path:
            self.send_error(HTTPStatus.NOT_FOUND)
            return

        try:
            content_length = int(self.headers.get("Content-Length", "0"))
        except ValueError:
            self.send_error(HTTPStatus.BAD_REQUEST, "Invalid Content-Length")
            return

        body = self.rfile.read(content_length)
        content_type = self.headers.get("Content-Type", "application/sdp")
        status, response_headers, response_body = relay_offer(
            self.server.probe_config.thermostat_target_url,
            body,
            content_type,
        )
        self.send_response(status)
        self.send_header(
            "Content-Type",
            response_headers.get("Content-Type", "text/plain; charset=utf-8"),
        )
        self.send_header("Content-Length", str(len(response_body)))
        self.end_headers()
        self.wfile.write(response_body)

    def do_OPTIONS(self) -> None:  # noqa: N802
        self.log_access("->")
        self.send_error(HTTPStatus.METHOD_NOT_ALLOWED)

    def log_access(self, direction: str, status: int | str = "-") -> None:
        timestamp = datetime.now().astimezone().isoformat(timespec="seconds")
        client = self.client_address[0] if self.client_address else "-"
        details = f"{direction} {timestamp} {client} {self.command} {self.path}"
        if status != "-":
            details = f"{details} {status}"
        print(details, file=sys.stderr)

    def log_request(self, code: int | str = "-", size: int | str = "-") -> None:
        self.log_access("<-", code)

    def log_message(self, format: str, *args: object) -> None:
        return


class ProbeServer(ThreadingHTTPServer):
    def __init__(self, server_address: tuple[str, int], probe_config: ProbeConfig):
        super().__init__(server_address, ProbeRequestHandler)
        self.probe_config = probe_config


def relay_offer(target_url: str, offer: bytes, content_type: str) -> tuple[int, Mapping[str, str], bytes]:
    request = Request(
        target_url,
        data=offer,
        method="POST",
        headers={"Content-Type": content_type},
    )
    try:
        with urlopen(request, timeout=30) as response:
            return response.status, response.headers, response.read()
    except HTTPError as exc:
        return exc.code, exc.headers, exc.read()
    except (URLError, TimeoutError, OSError, ProxyError) as exc:
        body = str(exc).encode("utf-8", errors="replace")
        return HTTPStatus.BAD_GATEWAY, {"Content-Type": "text/plain; charset=utf-8"}, body


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

    try:
        config = resolve_config(args, config_values)
    except (ConfigError, ValueError) as exc:
        print(f"theoprobe: {exc}", file=sys.stderr)
        return 1

    local_url = f"http://{config.listen_host}:{config.listen_port}/"
    print(f"theoprobe: open {local_url}", file=sys.stderr)
    print(f"theoprobe: proxying {config.proxy_path} -> {config.thermostat_target_url}", file=sys.stderr)

    server = ProbeServer((config.listen_host, config.listen_port), config)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        return 0
    finally:
        server.server_close()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
