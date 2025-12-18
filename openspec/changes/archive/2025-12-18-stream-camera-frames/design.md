# Design: MJPEG Camera Streaming

## Context

The thermostat runs on a DFRobot FireBeetle 2 ESP32-P4 board with MIPI CSI camera support. An OV5647 camera module will be connected via the CSI connector (Raspberry Pi camera compatible pinout). The stream is consumed by Frigate for ML-based object detection at 6fps.

## Goals / Non-Goals

**Goals:**
- Serve MJPEG stream at 6fps for Frigate consumption
- Support OV5647 camera via MIPI CSI
- Non-fatal boot if camera is absent/fails
- Configurable via Kconfig

**Non-Goals:**
- H.264 streaming (out of scope for initial implementation)
- Multiple resolution options
- Authentication on stream endpoint
- Snapshot/single-image endpoint

---

## Component Architecture

### Dependencies

| Component | Version | Purpose |
|-----------|---------|---------|
| `espressif/esp_video` | ^1.4.0 | V4L2-like video framework, exposes `/dev/video*` devices |
| `espressif/esp_cam_sensor` | ^1.6.0 | OV5647 driver, SCCB init, format configuration |
| `esp_http_server` | (IDF builtin) | HTTP server for MJPEG endpoint |
| `esp_ldo_regulator` | (IDF builtin) | LDO channel control for MIPI PHY power |

### esp_video Device Nodes

The esp_video component exposes Linux V4L2-style device nodes:

| Device | Purpose |
|--------|---------|
| `/dev/video0` | MIPI-CSI capture (raw frames from OV5647) |
| `/dev/video10` | JPEG M2M encoder (hardware accelerated) |

### Data Flow

```
OV5647 ──MIPI-CSI──▶ CSI Controller ──▶ ISP ──▶ /dev/video0 (RGB24)
                                                      │
                                                      ▼
                                              /dev/video10 (JPEG encoder)
                                                      │
                                                      ▼
                                              HTTP /stream endpoint
                                                      │
                                                      ▼
                                                  Frigate
```

---

## Implementation Details

### 1. LDO Power Configuration

MIPI CSI PHY requires 2.5V from LDO channel 3. Using `esp_ldo_regulator.h`:

```c
#include "esp_ldo_regulator.h"

esp_ldo_channel_handle_t ldo_mipi_phy;
esp_ldo_channel_config_t ldo_cfg = {
    .chan_id = 3,
    .voltage_mv = 2500,
};
esp_ldo_acquire_channel(&ldo_cfg, &ldo_mipi_phy);
```

Note: the display BSP also powers the MIPI PHY via the same LDO channel (3). If the LDO is already enabled, camera bring-up should treat that as non-fatal and proceed without taking ownership.

Cleanup uses `esp_ldo_release_channel()`.

### 2. Video Subsystem Initialization

Using `esp_video_init()` to initialize the CSI camera and sensor in one call. This higher-level API handles SCCB (I2C) bus setup, sensor detection, and V4L2 device registration:

```c
#include "esp_video_init.h"

// Build defaults select OV5647 RAW8 800x1280 mode via sdkconfig.defaults.
esp_video_init_csi_config_t csi_cfg = {
    .sccb_config = {
        .init_sccb = false,
        .i2c_handle = bsp_i2c_get_handle(),  // BSP-owned I2C bus on GPIO7/8
        .freq = 100000,
    },
    .reset_pin = -1,       // No reset pin
    .pwdn_pin = -1,        // No power down pin
    .dont_init_ldo = true, // We handle LDO ourselves
};

esp_video_init_config_t video_cfg = {
    .csi = &csi_cfg,
};

esp_video_init(&video_cfg);
```

Cleanup uses `esp_video_deinit()`.

### 3. V4L2 Video Capture

Open `/dev/video0` and configure capture using V4L2 ioctls:

```c
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>

int cam_fd = open("/dev/video0", O_RDWR);

// Set format
struct v4l2_format fmt = {
    .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
    .fmt.pix = {
        .width = 800,
        .height = 1280,
        .pixelformat = V4L2_PIX_FMT_RGB24,
    },
};
ioctl(cam_fd, VIDIOC_S_FMT, &fmt);

// Request buffers (mmap mode)
struct v4l2_requestbuffers req = {
    .count = 2,
    .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
    .memory = V4L2_MEMORY_MMAP,
};
ioctl(cam_fd, VIDIOC_REQBUFS, &req);

// Map buffers, queue them, start streaming
ioctl(cam_fd, VIDIOC_STREAMON, &type);
```

### 4. JPEG Encoding

Use `/dev/video10` hardware JPEG encoder (M2M device):

```c
int jpeg_fd = open("/dev/video10", O_RDWR);

// Configure output format (input to encoder)
struct v4l2_format out_fmt = {
    .type = V4L2_BUF_TYPE_VIDEO_OUTPUT,
    .fmt.pix = {
        .width = 800,
        .height = 640,
        .pixelformat = V4L2_PIX_FMT_RGB565,
    },
};
ioctl(jpeg_fd, VIDIOC_S_FMT, &out_fmt);

// Configure capture format (JPEG output)
struct v4l2_format cap_fmt = {
    .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
    .fmt.pix = {
        .pixelformat = V4L2_PIX_FMT_JPEG,
    },
};
ioctl(jpeg_fd, VIDIOC_S_FMT, &cap_fmt);

// Set JPEG quality
struct v4l2_control ctrl = {
    .id = V4L2_CID_JPEG_COMPRESSION_QUALITY,
    .value = CONFIG_THEO_MJPEG_JPEG_QUALITY,  // 70
};
ioctl(jpeg_fd, VIDIOC_S_CTRL, &ctrl);
```

### 5. HTTP MJPEG Streaming

Using esp_http_server with chunked transfer:

```c
#include "esp_http_server.h"

static esp_err_t stream_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "multipart/x-mixed-replace; boundary=frame");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");

    char part_header[128];
    const int frame_delay_ms = 1000 / CONFIG_THEO_MJPEG_STREAM_FPS;

    while (streaming) {
        // Dequeue frame from camera
        ioctl(cam_fd, VIDIOC_DQBUF, &cam_buf);

        // Encode to JPEG via /dev/video10
        // ... queue to encoder, dequeue result ...

        // Send MJPEG part
        snprintf(part_header, sizeof(part_header),
            "--frame\r\n"
            "Content-Type: image/jpeg\r\n"
            "Content-Length: %zu\r\n\r\n", jpeg_size);
        httpd_resp_send_chunk(req, part_header, strlen(part_header));
        httpd_resp_send_chunk(req, jpeg_buf, jpeg_size);
        httpd_resp_send_chunk(req, "\r\n", 2);

        // Requeue camera buffer
        ioctl(cam_fd, VIDIOC_QBUF, &cam_buf);

        // Rate limit to configured FPS
        vTaskDelay(pdMS_TO_TICKS(frame_delay_ms));
    }
    return ESP_OK;
}

httpd_config_t config = HTTPD_DEFAULT_CONFIG();
config.server_port = CONFIG_THEO_MJPEG_STREAM_PORT;
config.stack_size = 8192;
config.core_id = 1;  // Run on core 1 to avoid UI interference

httpd_uri_t stream_uri = {
    .uri = "/stream",
    .method = HTTP_GET,
    .handler = stream_handler,
};
```

---

## Hardware Configuration

From [DFRobot community example](https://community.dfrobot.com/makelog-318022.html):

| Parameter | Value | Notes |
|-----------|-------|-------|
| SCCB SDA | GPIO 7 | I2C data for sensor control |
| SCCB SCL | GPIO 8 | I2C clock for sensor control |
| Env sensor SDA | GPIO 50 | Separate I2C bus (no conflict) |
| Env sensor SCL | GPIO 52 | Separate I2C bus (no conflict) |
| LDO Channel | 3 | MIPI PHY power |
| LDO Voltage | 2500 mV | Required for CSI |
| Lane Bitrate | 200 Mbps | 2-lane MIPI CSI |

---

## Risks / Trade-offs

| Risk | Mitigation |
|------|------------|
| Memory pressure (~3-4MB PSRAM) | Use `heap_caps_malloc(MALLOC_CAP_SPIRAM)`; 32MB PSRAM available |
| I2C bus conflict | Camera on GPIO 7/8, env sensors on GPIO 50/52 (separate buses) |
| WiFi bandwidth (~2-4 Mbps) | Well within ESP-Hosted capacity (20+ Mbps) |
| CPU load from JPEG encoding | Hardware encoder; HTTP server on core 1 to avoid UI stutter |

## References

- [esp-video-components](https://github.com/espressif/esp-video-components) - V4L2 framework
- [esp_cam_sensor](https://components.espressif.com/components/espressif/esp_cam_sensor) - Camera drivers
- [DFRobot FireBeetle 2 Camera Example](https://community.dfrobot.com/makelog-318022.html) - GPIO pinout
- [DFRobot Camera Web Server](https://community.dfrobot.com/makelog-318082.html) - HTTP streaming example

## Open Questions

None - all clarifications obtained during planning.
