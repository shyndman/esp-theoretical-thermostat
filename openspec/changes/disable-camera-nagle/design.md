# Design: Camera Socket Optimizations

## Context
The MJPEG stream delivers high-resolution (1280x960) frames at 10 FPS. Each frame is sent in multiple `httpd_resp_send_chunk` calls (boundary, headers, JPEG data, footer). Nagle's algorithm can cause delays when sending these small chunks, especially if the client is using delayed ACKs.

## Architectural Changes
The `esp_http_server` component provides an `open_fn` callback in `httpd_config_t`. This callback is the correct place to apply socket-level optimizations to client connections.

### Socket Options
- **`TCP_NODELAY`**: Disables Nagle's algorithm. Critical for ensuring small HTTP chunks are sent without waiting for more data.
- **`TCP_QUICKACK`**: Disables delayed ACKs. While primarily a receiver-side optimization, applying it on the sender side can help in some stacks to ensure the congestion window isn't unnecessarily throttled by waiting for cumulative ACKs.
- **`IP_TOS`**: Set to `IPTOS_LOWDELAY` (0x10). Tells the Wi-Fi stack and network equipment to prioritize these packets for low latency.

## Implementation Plan
1.  Define `mjpeg_stream_on_sess_open` in `mjpeg_stream.c`.
2.  Use `setsockopt` with `IPPROTO_TCP` for `TCP_NODELAY` and `TCP_QUICKACK`.
3.  Use `setsockopt` with `IPPROTO_IP` for `IP_TOS`.
4.  Wire the callback into `start_http_server`.

## Trade-offs
- **Network Overhead**: Disabling Nagle can increase the number of small packets on the network, slightly reducing overall throughput efficiency. However, for a high-bitrate MJPEG stream, the overhead of headers relative to JPEG data is negligible.
- **Power Consumption**: More frequent radio transmissions can slightly increase power draw, but the device is wall-powered.
