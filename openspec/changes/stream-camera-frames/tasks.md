# Tasks: stream-camera-frames

## Implementation

- [x] 1.1 Add `espressif/esp_video` and `espressif/esp_cam_sensor` to `main/idf_component.yml`
- [x] 1.2 Add "Camera & Streaming" menu to `main/Kconfig.projbuild`
- [x] 1.3 Update `main/CMakeLists.txt` with conditional streaming sources and `esp_http_server` dependency
- [x] 1.4 Create `main/streaming/mjpeg_stream.h` with public API
- [x] 1.5 Create `main/streaming/mjpeg_stream.c` with camera init, HTTP server, and MJPEG handler
- [x] 1.6 Integrate `mjpeg_stream_start()` into `main/app_main.c` boot sequence
- [x] 1.7 Build and verify camera detection on hardware
- [x] 1.8 Test MJPEG stream endpoint with curl/browser
- [x] 1.9 Validate Frigate integration
