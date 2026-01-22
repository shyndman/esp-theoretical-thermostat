# Change Proposal: Simplify with MJPEG

## Why
The existing H.264 stream (800x800 @ 15 FPS) is overkill for the thermostat’s needs and adds complexity without delivering the requested “bigger” grayscale video. Moving to 1280x960 @ 10 FPS MJPEG leverages the ESP32-P4 hardware JPEG encoder, simplifies the streaming path, and produces higher-resolution frames better suited for consumers like Frigate.

## What Changes
Replace the H.264 streaming stack with a grayscale MJPEG pipeline sourced from the OV5647 at 1280x960 @ 10 FPS, encoded via the on-chip JPEG engine, and exposed over HTTP as `multipart/x-mixed-replace`.

### Architecture
*   **Capture:** V4L2 capture from `/dev/video0` (OV5647) configured for 1280x960 @ 10 FPS.
*   **Encoding:** Hardware JPEG encoder driver used directly to encode captured frames into Grayscale JPEG images.
*   **Streaming:** HTTP server serves the JPEG frames as a `multipart/x-mixed-replace` stream.

### Key Changes
1.  **Resolution Upgrade:** 800x800 -> 1280x960.
2.  **Framerate Reduction:** 15 FPS -> 10 FPS.
3.  **Format Change:** H.264 -> MJPEG (Grayscale).
4.  **Component Replacement:** `h264_stream` component replaced by `mjpeg_stream`.

### Trade-offs
*   **Bandwidth:** MJPEG generally uses more bandwidth than H.264 for the same quality/resolution, but at 10 FPS grayscale, it is manageable and simplifies the client-side decoding.
*   **Latency:** MJPEG latency is typically very low (frame-by-frame).
*   **Complexity:** Significant reduction in complexity by removing the H.264 V4L2 M2M pipeline.

## Dependencies
This implementation relies on the following ESP-IDF components:
- **espressif/esp_video (^1.4.0)**: Provides the V4L2 device framework and hardware-abstracted video headers.
- **espressif/esp_cam_sensor (^1.6.0)**: Provides drivers for the OV5647 camera sensor.
- **Hardware JPEG Encoder Driver**: Built into the ESP-IDF for ESP32-P4 (available via `driver/jpeg_encode.h`).
