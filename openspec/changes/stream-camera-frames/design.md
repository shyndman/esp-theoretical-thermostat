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
| `esp_ldo` | (IDF builtin) | LDO channel control for MIPI PHY power |

### esp_video Device Nodes

The esp_video component exposes Linux V4L2-style device nodes:

| Device | Purpose |
|--------|---------|
| `/dev/video0` | MIPI-CSI capture (raw frames from OV5647) |
| `/dev/video10` | JPEG M2M encoder (hardware accelerated) |

### Data Flow

```
OV5647 ──MIPI-CSI──▶ CSI Controller ──▶ ISP ──▶ /dev/video0 (RGB565)
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

MIPI CSI PHY requires 2.5V from LDO channel 3:

```c
#include "esp_ldo.h"

esp_ldo_unit_handle_t ldo_mipi_phy;
esp_ldo_unit_init_cfg_t ldo_cfg = {
    .unit_id = LDO_UNIT_3,
    .cfg = {
        .voltage_mv = 2500,
    },
};
esp_ldo_init_unit(&ldo_cfg, &ldo_mipi_phy);
esp_ldo_enable_unit(ldo_mipi_phy);
```

### 2. Camera Sensor Initialization

Using esp_cam_sensor to probe and configure OV5647:

```c
#include "esp_cam_sensor.h"

esp_cam_sensor_config_t cam_cfg = {
    .sccb_config = {
        .i2c_port = I2C_NUM_0,
        .scl_pin = CONFIG_THEO_CAMERA_SCCB_SCL_GPIO,  // GPIO 8
        .sda_pin = CONFIG_THEO_CAMERA_SCCB_SDA_GPIO,  // GPIO 7
    },
    .sensor_port = ESP_CAM_SENSOR_MIPI_CSI,
};

// esp_cam_sensor will auto-detect OV5647 by probing I2C addresses
esp_cam_sensor_handle_t sensor;
esp_cam_sensor_detect(&cam_cfg, &sensor);

// Set format (OV5647 outputs RAW8, ISP converts to RGB565)
esp_cam_sensor_format_t fmt = {
    .name = "MIPI_2lane_24Minput_RAW8_800x640_50fps",
};
esp_cam_sensor_set_format(sensor, &fmt);
```

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
        .height = 640,
        .pixelformat = V4L2_PIX_FMT_RGB565,
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

// Configure output format
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

Using esp_http_server:

```c
#include "esp_http_server.h"

static esp_err_t stream_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "multipart/x-mixed-replace; boundary=frame");

    char part_header[64];
    while (streaming) {
        // Dequeue frame from camera
        ioctl(cam_fd, VIDIOC_DQBUF, &buf);

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
        ioctl(cam_fd, VIDIOC_QBUF, &buf);

        // Rate limit to 6fps
        vTaskDelay(pdMS_TO_TICKS(166));
    }
    return ESP_OK;
}

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
| LDO Channel | 3 | MIPI PHY power |
| LDO Voltage | 2500 mV | Required for CSI |
| Lane Bitrate | 200 Mbps | 2-lane MIPI CSI |

**Note:** Environmental sensors are on different GPIOs (confirmed no conflict).

---

## Risks / Trade-offs

| Risk | Mitigation |
|------|------------|
| Memory pressure (~3-4MB PSRAM) | Use `heap_caps_malloc(MALLOC_CAP_SPIRAM)`; 32MB PSRAM available |
| I2C bus conflict | Verified env sensors on different GPIOs |
| WiFi bandwidth (~2-4 Mbps) | Well within ESP-Hosted capacity (20+ Mbps) |
| CPU load from JPEG encoding | Hardware encoder; task priority below UI |

## References

- [esp-video-components](https://github.com/espressif/esp-video-components) - V4L2 framework
- [esp_cam_sensor](https://components.espressif.com/components/espressif/esp_cam_sensor) - Camera drivers
- [DFRobot FireBeetle 2 Camera Example](https://community.dfrobot.com/makelog-318022.html) - GPIO pinout
- [DFRobot Camera Web Server](https://community.dfrobot.com/makelog-318082.html) - HTTP streaming example

## Open Questions

None - all clarifications obtained during planning.
