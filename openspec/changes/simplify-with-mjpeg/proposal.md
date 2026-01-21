# Change Proposal: Simplify with MJPEG

## Summary
Replace the existing H.264 video streaming implementation with a simpler, higher-resolution MJPEG stream using the ESP32-P4's hardware JPEG encoder.

## Motivation
The current H.264 stream at 800x800 is being replaced to better suit the device's usage. The new requirement is for a higher resolution (1280x960), lower framerate (5 FPS), grayscale MJPEG stream. This leverages the hardware JPEG encoder on the ESP32-P4, simplifies the pipeline by removing H.264 complexity, and provides "bigger" video as requested.

## Dependencies
This implementation relies on the following ESP-IDF components:
- **espressif/esp_video (^1.4.0)**: Provides the V4L2 device framework and hardware-abstracted video headers.
- **espressif/esp_cam_sensor (^1.6.0)**: Provides drivers for the OV5647 camera sensor.
- **Hardware JPEG Encoder Driver**: Built into the ESP-IDF for ESP32-P4 (available via `driver/jpeg_encode.h`).

## Design
### Architecture
*   **Capture:** V4L2 capture from `/dev/video0` (OV5647) configured for 1280x960 @ 5 FPS.
*   **Encoding:** Hardware JPEG encoder driver used directly to encode captured frames into Grayscale JPEG images.
*   **Streaming:** HTTP server serves the JPEG frames as a `multipart/x-mixed-replace` stream.

### Key Changes
1.  **Resolution Upgrade:** 800x800 -> 1280x960.
2.  **Framerate Reduction:** 15 FPS -> 5 FPS.
3.  **Format Change:** H.264 -> MJPEG (Grayscale).
4.  **Component Replacement:** `h264_stream` component replaced by `mjpeg_stream`.

### Trade-offs
*   **Bandwidth:** MJPEG generally uses more bandwidth than H.264 for the same quality/resolution, but at 5 FPS grayscale, it is manageable and simplifies the client-side decoding.
*   **Latency:** MJPEG latency is typically very low (frame-by-frame).
*   **Complexity:** Significant reduction in complexity by removing the H.264 V4L2 M2M pipeline.
