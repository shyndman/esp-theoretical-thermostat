## Context
1. The current camera stream uses V4L2 capture on `/dev/video0` (RGB24) and a JPEG M2M encoder on `/dev/video10`, then serves multipart MJPEG at `/stream`.
2. The single consumer is Frigate/Go2RTC, which supports raw Annex-B H.264 over HTTP.
3. MJPEG reliability and bandwidth are the pain points; the goal is to move to H.264 with minimal surface-area changes.

## Goals / Non-Goals
1. Goals:
   a. Stream raw Annex-B H.264 over HTTP at `/stream`.
   b. Default to 800x800 resolution and ~8 fps.
   c. Keep initialization non-blocking and consistent with the boot flow.
2. Non-goals:
   a. RTSP/RTP support or multi-client handling.
   b. Dual-mode MJPEG/H.264 toggles.
   c. Exposed bitrate/QP tuning via Kconfig (defaults only).

## Proposed Design
1. Capture pipeline:
   a. Configure `/dev/video0` for 800x800 YUV420 output (using ISP conversion if required by the sensor path).
2. Encoding:
   a. Use V4L2 M2M H.264 encoder at `/dev/video11`.
   b. Input format: `V4L2_PIX_FMT_YUV420`; output format: `V4L2_PIX_FMT_H264`.
   c. Set `V4L2_CID_MPEG_VIDEO_H264_I_PERIOD` to the configured FPS to ensure frequent IDR frames.
   d. Keep bitrate/QP defaults in code (tunable after Frigate validation).
3. HTTP streaming:
   a. Keep the same HTTP server and `/stream` endpoint.
   b. Response content-type: `video/h264`.
   c. Stream the Annex-B byte stream as a single chunked response without multipart boundaries.

## Configuration
1. Rename stream configuration keys:
   a. `THEO_MJPEG_STREAM_PORT` -> `THEO_H264_STREAM_PORT`.
   b. `THEO_MJPEG_STREAM_FPS` -> `THEO_H264_STREAM_FPS` (default 8).
2. Remove JPEG quality configuration.

## Risks
1. If `/dev/video0` cannot deliver YUV420 directly, the ISP path must be used; failure to obtain YUV420 is a hard stop for H.264.
2. The encoder must emit SPS/PPS for IDR frames; verify with Frigate/Go2RTC on first integration.
