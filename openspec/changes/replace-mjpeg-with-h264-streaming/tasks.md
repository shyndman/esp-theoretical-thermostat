## 1. Implementation
- [ ] 1.1 Rename streaming Kconfig symbols to `THEO_H264_STREAM_PORT` and `THEO_H264_STREAM_FPS`, and remove JPEG quality settings.
- [ ] 1.2 Replace MJPEG encoder path with H.264 M2M on `/dev/video11`, configuring YUV420 input at 1024x768.
- [ ] 1.3 Update the HTTP streaming handler to emit raw Annex-B H.264 with `Content-Type: video/h264` on `/stream`.
- [ ] 1.4 Update boot messaging and any MJPEG-specific identifiers to H.264 equivalents.

## 2. Validation
- [ ] 2.1 Run `idf.py build`.
- [ ] 2.2 Validate streaming with Frigate/Go2RTC at 1024x768@8fps and record observations in `docs/manual-test-plan.md`.
