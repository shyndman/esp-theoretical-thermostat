# Change: Disable Nagle's Algorithm on Camera HTTP Server

## Why
The camera stream experiences micro-stutters or bursts of frames caused by Nagle's algorithm buffering small packets (like HTTP headers or JPEG frame bytes) while waiting for ACKs, which conflicts with real-time video streaming requirements.

## What Changes
- Implement a session open callback (`open_fn`) for the MJPEG HTTP server
- Set `TCP_NODELAY` on new session sockets to transmit JPEG chunks immediately
- Apply `IP_TOS` socket optimization for low-latency packet delivery
- Update camera-streaming requirements to include socket optimizations

## Impact
- Affected specs: `socket-optimizations`
- Affected code: `main/streaming/mjpeg_stream.c`
