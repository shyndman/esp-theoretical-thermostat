# Tasks: stream-camera-frames

## Implementation

- [ ] 1.1 Add `espressif/esp_video` and `espressif/esp_cam_sensor` to `main/idf_component.yml`
- [ ] 1.2 Add "Camera & Streaming" menu to `main/Kconfig.projbuild`
- [ ] 1.3 Update `main/CMakeLists.txt` with conditional streaming sources and `esp_http_server` dependency
- [ ] 1.4 Create `main/streaming/mjpeg_stream.h` with public API
- [ ] 1.5 Create `main/streaming/mjpeg_stream.c` with camera init, HTTP server, and MJPEG handler
- [ ] 1.6 Integrate `mjpeg_stream_start()` into `main/app_main.c` boot sequence
- [ ] 1.7 Build and verify camera detection on hardware
- [ ] 1.8 Test MJPEG stream endpoint with curl/browser
- [ ] 1.9 Validate Frigate integration
