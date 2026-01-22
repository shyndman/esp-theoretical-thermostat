# Proposal: Disable Nagle's Algorithm on Camera HTTP Server

## Summary
This change disables Nagle's algorithm on the camera's HTTP server to improve MJPEG streaming smoothness. By setting `TCP_NODELAY` on accepted sockets, we ensure that JPEG chunks are transmitted immediately, reducing latency-induced stutters caused by the interaction between Nagle's algorithm and delayed ACKs.

## Problem
The camera stream sometimes experiences micro-stutters or bursts of frames. This is a common symptom of Nagle's algorithm buffering small packets (like HTTP headers or the final bytes of a JPEG frame) while waiting for an ACK or more data, which clashes with the real-time requirements of video streaming.

## Solution
1. Implement a session open callback (`open_fn`) for the MJPEG HTTP server.
2. Within the callback, use `setsockopt` to enable `TCP_NODELAY` on the new session socket.
3. Apply additional low-latency socket optimization (`IP_TOS`) to further ensure smooth delivery.

## Scope
- `main/streaming/mjpeg_stream.c`: Implement `open_fn` and register it during server start.
- `openspec/specs/camera-streaming/spec.md`: Update requirements to include socket optimizations.
