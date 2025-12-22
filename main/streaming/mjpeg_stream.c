#include "mjpeg_stream.h"

#include <fcntl.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "esp_log.h"
#include "esp_err.h"
#include "esp_http_server.h"
#include "esp_heap_caps.h"
#include "esp_ldo_regulator.h"
#include "esp_video_init.h"
#include "linux/videodev2.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "bsp/esp32_p4_nano.h"
#include "inttypes.h"
#include "connectivity/wifi_remote_manager.h"

static const char *TAG = "mjpeg_stream";

#define FRAME_WIDTH   800
#define FRAME_HEIGHT  1280
#define CAM_BUF_COUNT 2
#define JPEG_BUF_COUNT 2
#define JPEG_ENCODE_TIMEOUT_MS 200

typedef struct {
  void *start;
  size_t length;
} buffer_t;

static esp_ldo_channel_handle_t s_ldo_mipi_phy = NULL;
static int s_cam_fd = -1;
static int s_jpeg_fd = -1;
static buffer_t s_cam_buffers[CAM_BUF_COUNT];
static buffer_t s_jpeg_out_buffers[JPEG_BUF_COUNT];
static buffer_t s_jpeg_cap_buffers[JPEG_BUF_COUNT];
static httpd_handle_t s_httpd = NULL;
static volatile bool s_streaming = false;
static bool s_video_initialized = false;
static jpeg_encoder_handle_t s_jpeg_handle = NULL;

static void fourcc_to_str(uint32_t fourcc, char out[5])
{
  out[0] = (char)(fourcc & 0xFF);
  out[1] = (char)((fourcc >> 8) & 0xFF);
  out[2] = (char)((fourcc >> 16) & 0xFF);
  out[3] = (char)((fourcc >> 24) & 0xFF);
  out[4] = '\0';
}

static esp_err_t init_ldo(void)
{
  esp_ldo_channel_config_t ldo_cfg = {
    .chan_id = 3,
    .voltage_mv = 2500,
  };

  esp_err_t err = esp_ldo_acquire_channel(&ldo_cfg, &s_ldo_mipi_phy);
  if (err == ESP_ERR_INVALID_STATE) {
    // The display path also powers the MIPI PHY (LDO channel 3). If it's already
    // enabled, we can proceed without taking ownership.
    ESP_LOGI(TAG, "MIPI PHY LDO already enabled; skipping acquire");
    s_ldo_mipi_phy = NULL;
    return ESP_OK;
  }
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to acquire LDO channel: %s", esp_err_to_name(err));
    return err;
  }

  ESP_LOGI(TAG, "MIPI PHY LDO enabled (2500mV)");
  return ESP_OK;
}

static esp_err_t init_video_subsystem(void)
{
  jpeg_encode_engine_cfg_t enc_cfg = {
    .intr_priority = 0,
    .timeout_ms = JPEG_ENCODE_TIMEOUT_MS,
  };
  esp_video_init_jpeg_config_t jpeg_cfg = {
    .enc_handle = NULL,
  };

  i2c_master_bus_handle_t i2c_handle = bsp_i2c_get_handle();
  if (i2c_handle == NULL) {
    ESP_LOGE(TAG, "bsp_i2c_get_handle returned NULL");
    return ESP_ERR_INVALID_STATE;
  }

  esp_err_t err = jpeg_new_encoder_engine(&enc_cfg, &s_jpeg_handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to create JPEG encoder: %s", esp_err_to_name(err));
    return err;
  }

  jpeg_cfg.enc_handle = s_jpeg_handle;

  // Configure CSI with SCCB (I2C) for camera sensor
  esp_video_init_csi_config_t csi_cfg = {
    .sccb_config = {
      .init_sccb = false,
      .i2c_handle = i2c_handle,
      .freq = 100000,
    },
    .reset_pin = -1,  // No reset pin
    .pwdn_pin = -1,   // No power down pin
    .dont_init_ldo = true,  // We initialized LDO ourselves
  };

  esp_video_init_config_t video_cfg = {
    .csi = &csi_cfg,
    .jpeg = &jpeg_cfg,
  };

  err = esp_video_init(&video_cfg);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to init esp_video: %s", esp_err_to_name(err));
    jpeg_del_encoder_engine(s_jpeg_handle);
    s_jpeg_handle = NULL;
    return err;
  }

  s_video_initialized = true;
  ESP_LOGI(TAG, "JPEG encoder timeout set to %d ms", JPEG_ENCODE_TIMEOUT_MS);
  ESP_LOGI(TAG, "Video subsystem initialized");
  return ESP_OK;
}

static esp_err_t init_v4l2_capture(void)
{
  s_cam_fd = open("/dev/video0", O_RDWR);
  if (s_cam_fd < 0) {
    ESP_LOGW(TAG, "Failed to open /dev/video0 - camera may not be connected");
    return ESP_ERR_NOT_FOUND;
  }

  struct v4l2_format fmt = {
    .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
    .fmt.pix = {
      .width = FRAME_WIDTH,
      .height = FRAME_HEIGHT,
      .pixelformat = V4L2_PIX_FMT_RGB24,
    },
  };

  if (ioctl(s_cam_fd, VIDIOC_S_FMT, &fmt) < 0) {
    ESP_LOGE(TAG, "Failed to set capture format");
    return ESP_FAIL;
  }

  struct v4l2_format active_fmt = {
    .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
  };
  if (ioctl(s_cam_fd, VIDIOC_G_FMT, &active_fmt) == 0) {
    char requested_fourcc[5];
    char active_fourcc[5];
    fourcc_to_str(fmt.fmt.pix.pixelformat, requested_fourcc);
    fourcc_to_str(active_fmt.fmt.pix.pixelformat, active_fourcc);
    ESP_LOGI(TAG,
             "Capture format: requested=%ux%u %s, active=%ux%u %s (bytesperline=%u sizeimage=%u)",
             fmt.fmt.pix.width, fmt.fmt.pix.height, requested_fourcc,
             active_fmt.fmt.pix.width, active_fmt.fmt.pix.height, active_fourcc,
             active_fmt.fmt.pix.bytesperline, active_fmt.fmt.pix.sizeimage);
  } else {
    ESP_LOGW(TAG, "Failed to query active capture format");
  }

  struct v4l2_requestbuffers req = {
    .count = CAM_BUF_COUNT,
    .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
    .memory = V4L2_MEMORY_MMAP,
  };

  if (ioctl(s_cam_fd, VIDIOC_REQBUFS, &req) < 0) {
    ESP_LOGE(TAG, "Failed to request capture buffers");
    return ESP_FAIL;
  }

  for (int i = 0; i < CAM_BUF_COUNT; i++) {
    struct v4l2_buffer buf = {
      .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
      .memory = V4L2_MEMORY_MMAP,
      .index = i,
    };

    if (ioctl(s_cam_fd, VIDIOC_QUERYBUF, &buf) < 0) {
      ESP_LOGE(TAG, "Failed to query capture buffer %d", i);
      return ESP_FAIL;
    }

    s_cam_buffers[i].length = buf.length;
    s_cam_buffers[i].start = mmap(NULL, buf.length, PROT_READ | PROT_WRITE,
                                   MAP_SHARED, s_cam_fd, buf.m.offset);
    if (s_cam_buffers[i].start == MAP_FAILED) {
      ESP_LOGE(TAG, "Failed to mmap capture buffer %d", i);
      return ESP_FAIL;
    }

    if (ioctl(s_cam_fd, VIDIOC_QBUF, &buf) < 0) {
      ESP_LOGE(TAG, "Failed to queue capture buffer %d", i);
      return ESP_FAIL;
    }
  }

  enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if (ioctl(s_cam_fd, VIDIOC_STREAMON, &type) < 0) {
    ESP_LOGE(TAG, "Failed to start capture stream");
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "V4L2 capture initialized (%dx%d RGB24)", FRAME_WIDTH, FRAME_HEIGHT);
  return ESP_OK;
}

static esp_err_t init_jpeg_encoder(void)
{
  s_jpeg_fd = open("/dev/video10", O_RDWR);
  if (s_jpeg_fd < 0) {
    ESP_LOGE(TAG, "Failed to open /dev/video10 (JPEG encoder)");
    return ESP_FAIL;
  }

  // Configure output format (input to encoder: RGB24)
  struct v4l2_format out_fmt = {
    .type = V4L2_BUF_TYPE_VIDEO_OUTPUT,
    .fmt.pix = {
      .width = FRAME_WIDTH,
      .height = FRAME_HEIGHT,
      .pixelformat = V4L2_PIX_FMT_RGB24,
    },
  };

  if (ioctl(s_jpeg_fd, VIDIOC_S_FMT, &out_fmt) < 0) {
    ESP_LOGE(TAG, "Failed to set JPEG encoder input format");
    return ESP_FAIL;
  }

  struct v4l2_format active_out_fmt = {
    .type = V4L2_BUF_TYPE_VIDEO_OUTPUT,
  };
  if (ioctl(s_jpeg_fd, VIDIOC_G_FMT, &active_out_fmt) == 0) {
    char requested_fourcc[5];
    char active_fourcc[5];
    fourcc_to_str(out_fmt.fmt.pix.pixelformat, requested_fourcc);
    fourcc_to_str(active_out_fmt.fmt.pix.pixelformat, active_fourcc);
    ESP_LOGI(TAG,
             "JPEG input format: requested=%ux%u %s, active=%ux%u %s (bytesperline=%u sizeimage=%u)",
             out_fmt.fmt.pix.width, out_fmt.fmt.pix.height, requested_fourcc,
             active_out_fmt.fmt.pix.width, active_out_fmt.fmt.pix.height, active_fourcc,
             active_out_fmt.fmt.pix.bytesperline, active_out_fmt.fmt.pix.sizeimage);
  } else {
    ESP_LOGW(TAG, "Failed to query active JPEG input format");
  }

  // Configure capture format (output from encoder: JPEG)
  struct v4l2_format cap_fmt = {
    .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
    .fmt.pix = {
      .width = FRAME_WIDTH,
      .height = FRAME_HEIGHT,
      .pixelformat = V4L2_PIX_FMT_JPEG,
    },
  };

  if (ioctl(s_jpeg_fd, VIDIOC_S_FMT, &cap_fmt) < 0) {
    ESP_LOGE(TAG, "Failed to set JPEG encoder output format");
    return ESP_FAIL;
  }

  struct v4l2_format active_cap_fmt = {
    .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
  };
  if (ioctl(s_jpeg_fd, VIDIOC_G_FMT, &active_cap_fmt) == 0) {
    char requested_fourcc[5];
    char active_fourcc[5];
    fourcc_to_str(cap_fmt.fmt.pix.pixelformat, requested_fourcc);
    fourcc_to_str(active_cap_fmt.fmt.pix.pixelformat, active_fourcc);
    ESP_LOGI(TAG,
             "JPEG output format: requested=%ux%u %s, active=%ux%u %s (bytesperline=%u sizeimage=%u)",
             cap_fmt.fmt.pix.width, cap_fmt.fmt.pix.height, requested_fourcc,
             active_cap_fmt.fmt.pix.width, active_cap_fmt.fmt.pix.height, active_fourcc,
             active_cap_fmt.fmt.pix.bytesperline, active_cap_fmt.fmt.pix.sizeimage);
  } else {
    ESP_LOGW(TAG, "Failed to query active JPEG output format");
  }

  // Set JPEG quality
  struct v4l2_control ctrl = {
    .id = V4L2_CID_JPEG_COMPRESSION_QUALITY,
    .value = CONFIG_THEO_MJPEG_JPEG_QUALITY,
  };

  if (ioctl(s_jpeg_fd, VIDIOC_S_CTRL, &ctrl) < 0) {
    ESP_LOGW(TAG, "Failed to set JPEG quality (using default)");
  }

  // Request output buffers (input to encoder)
  struct v4l2_requestbuffers out_req = {
    .count = JPEG_BUF_COUNT,
    .type = V4L2_BUF_TYPE_VIDEO_OUTPUT,
    .memory = V4L2_MEMORY_MMAP,
  };

  if (ioctl(s_jpeg_fd, VIDIOC_REQBUFS, &out_req) < 0) {
    ESP_LOGE(TAG, "Failed to request JPEG output buffers");
    return ESP_FAIL;
  }

  // Request capture buffers (output from encoder)
  struct v4l2_requestbuffers cap_req = {
    .count = JPEG_BUF_COUNT,
    .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
    .memory = V4L2_MEMORY_MMAP,
  };

  if (ioctl(s_jpeg_fd, VIDIOC_REQBUFS, &cap_req) < 0) {
    ESP_LOGE(TAG, "Failed to request JPEG capture buffers");
    return ESP_FAIL;
  }

  // Map output buffers
  for (int i = 0; i < JPEG_BUF_COUNT; i++) {
    struct v4l2_buffer buf = {
      .type = V4L2_BUF_TYPE_VIDEO_OUTPUT,
      .memory = V4L2_MEMORY_MMAP,
      .index = i,
    };

    if (ioctl(s_jpeg_fd, VIDIOC_QUERYBUF, &buf) < 0) {
      ESP_LOGE(TAG, "Failed to query JPEG output buffer %d", i);
      return ESP_FAIL;
    }

    s_jpeg_out_buffers[i].length = buf.length;
    s_jpeg_out_buffers[i].start = mmap(NULL, buf.length, PROT_READ | PROT_WRITE,
                                        MAP_SHARED, s_jpeg_fd, buf.m.offset);
    if (s_jpeg_out_buffers[i].start == MAP_FAILED) {
      ESP_LOGE(TAG, "Failed to mmap JPEG output buffer %d", i);
      return ESP_FAIL;
    }
  }

  // Map capture buffers
  for (int i = 0; i < JPEG_BUF_COUNT; i++) {
    struct v4l2_buffer buf = {
      .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
      .memory = V4L2_MEMORY_MMAP,
      .index = i,
    };

    if (ioctl(s_jpeg_fd, VIDIOC_QUERYBUF, &buf) < 0) {
      ESP_LOGE(TAG, "Failed to query JPEG capture buffer %d", i);
      return ESP_FAIL;
    }

    s_jpeg_cap_buffers[i].length = buf.length;
    s_jpeg_cap_buffers[i].start = mmap(NULL, buf.length, PROT_READ | PROT_WRITE,
                                        MAP_SHARED, s_jpeg_fd, buf.m.offset);
    if (s_jpeg_cap_buffers[i].start == MAP_FAILED) {
      ESP_LOGE(TAG, "Failed to mmap JPEG capture buffer %d", i);
      return ESP_FAIL;
    }

    // Pre-queue capture buffers
    if (ioctl(s_jpeg_fd, VIDIOC_QBUF, &buf) < 0) {
      ESP_LOGE(TAG, "Failed to queue JPEG capture buffer %d", i);
      return ESP_FAIL;
    }
  }

  // Start encoder streams
  enum v4l2_buf_type out_type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
  enum v4l2_buf_type cap_type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

  if (ioctl(s_jpeg_fd, VIDIOC_STREAMON, &out_type) < 0) {
    ESP_LOGE(TAG, "Failed to start JPEG output stream");
    return ESP_FAIL;
  }

  if (ioctl(s_jpeg_fd, VIDIOC_STREAMON, &cap_type) < 0) {
    ESP_LOGE(TAG, "Failed to start JPEG capture stream");
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "JPEG encoder initialized (quality=%d)", CONFIG_THEO_MJPEG_JPEG_QUALITY);
  return ESP_OK;
}

static esp_err_t encode_frame_to_jpeg(void *rgb_data, size_t rgb_size,
                                       void **jpeg_data, size_t *jpeg_size)
{
  static int buf_idx = 0;

  // Copy RGB data to encoder input buffer
  struct v4l2_buffer out_buf = {
    .type = V4L2_BUF_TYPE_VIDEO_OUTPUT,
    .memory = V4L2_MEMORY_MMAP,
    .index = buf_idx,
  };

  memcpy(s_jpeg_out_buffers[buf_idx].start, rgb_data, rgb_size);
  out_buf.bytesused = rgb_size;

  if (ioctl(s_jpeg_fd, VIDIOC_QBUF, &out_buf) < 0) {
    ESP_LOGE(TAG, "Failed to queue frame for encoding");
    return ESP_FAIL;
  }

  // Wait for encoding to complete
  struct v4l2_buffer cap_buf = {
    .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
    .memory = V4L2_MEMORY_MMAP,
  };

  if (ioctl(s_jpeg_fd, VIDIOC_DQBUF, &cap_buf) < 0) {
    ESP_LOGE(TAG, "Failed to dequeue encoded frame");
    return ESP_FAIL;
  }

  *jpeg_data = s_jpeg_cap_buffers[cap_buf.index].start;
  *jpeg_size = cap_buf.bytesused;

  // Dequeue output buffer
  if (ioctl(s_jpeg_fd, VIDIOC_DQBUF, &out_buf) < 0) {
    ESP_LOGE(TAG, "Failed to dequeue encoder input buffer");
    return ESP_FAIL;
  }

  // Re-queue capture buffer
  if (ioctl(s_jpeg_fd, VIDIOC_QBUF, &cap_buf) < 0) {
    ESP_LOGE(TAG, "Failed to re-queue capture buffer");
    return ESP_FAIL;
  }

  buf_idx = (buf_idx + 1) % JPEG_BUF_COUNT;
  return ESP_OK;
}

static esp_err_t stream_handler(httpd_req_t *req)
{
  ESP_LOGI(TAG, "Stream client connected");

  esp_err_t err = httpd_resp_set_type(req, "multipart/x-mixed-replace; boundary=frame");
  if (err != ESP_OK) {
    return err;
  }

  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_hdr(req, "Cache-Control", "no-cache");

  char part_header[128];
  const int frame_delay_ms = 1000 / CONFIG_THEO_MJPEG_STREAM_FPS;
  const size_t expected_rgb_bytes = (size_t)FRAME_WIDTH * (size_t)FRAME_HEIGHT * 3;
  bool logged_first_frame = false;

  while (s_streaming) {
    // Dequeue camera frame
    struct v4l2_buffer cam_buf = {
      .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
      .memory = V4L2_MEMORY_MMAP,
    };

    if (ioctl(s_cam_fd, VIDIOC_DQBUF, &cam_buf) < 0) {
      ESP_LOGE(TAG, "Failed to dequeue camera frame");
      break;
    }

    if (!logged_first_frame) {
      ESP_LOGI(TAG, "First frame: bytesused=%" PRIu32 " expected=%zu", cam_buf.bytesused, expected_rgb_bytes);
      logged_first_frame = true;
    }

    // Encode to JPEG
    void *jpeg_data = NULL;
    size_t jpeg_size = 0;
    err = encode_frame_to_jpeg(s_cam_buffers[cam_buf.index].start,
                                cam_buf.bytesused, &jpeg_data, &jpeg_size);

    // Re-queue camera buffer
    if (ioctl(s_cam_fd, VIDIOC_QBUF, &cam_buf) < 0) {
      ESP_LOGE(TAG, "Failed to re-queue camera buffer");
      break;
    }

    if (err != ESP_OK || jpeg_data == NULL) {
      ESP_LOGE(TAG, "Frame encoding failed");
      continue;
    }

    // Send MJPEG part
    int header_len = snprintf(part_header, sizeof(part_header),
      "--frame\r\n"
      "Content-Type: image/jpeg\r\n"
      "Content-Length: %zu\r\n\r\n", jpeg_size);

    if (httpd_resp_send_chunk(req, part_header, header_len) != ESP_OK) {
      ESP_LOGI(TAG, "Stream client disconnected");
      break;
    }

    if (httpd_resp_send_chunk(req, jpeg_data, jpeg_size) != ESP_OK) {
      break;
    }

    if (httpd_resp_send_chunk(req, "\r\n", 2) != ESP_OK) {
      break;
    }

    vTaskDelay(pdMS_TO_TICKS(frame_delay_ms));
  }

  return ESP_OK;
}

static esp_err_t start_http_server(void)
{
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = CONFIG_THEO_MJPEG_STREAM_PORT;
  config.stack_size = 8192;
  config.core_id = 1;  // Run on core 1 to avoid UI interference

  esp_err_t err = httpd_start(&s_httpd, &config);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(err));
    return err;
  }

  httpd_uri_t stream_uri = {
    .uri = "/stream",
    .method = HTTP_GET,
    .handler = stream_handler,
    .user_ctx = NULL,
  };

  err = httpd_register_uri_handler(s_httpd, &stream_uri);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to register stream handler: %s", esp_err_to_name(err));
    httpd_stop(s_httpd);
    s_httpd = NULL;
    return err;
  }

  ESP_LOGI(TAG, "HTTP server started on port %d", CONFIG_THEO_MJPEG_STREAM_PORT);
  return ESP_OK;
}

esp_err_t mjpeg_stream_start(void)
{
  esp_err_t err;

  ESP_LOGI(TAG, "Starting MJPEG stream server...");

  err = init_ldo();
  if (err != ESP_OK) {
    return err;
  }

  err = init_video_subsystem();
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Video subsystem init failed; camera may not be connected");
    return ESP_ERR_NOT_FOUND;
  }

  err = init_v4l2_capture();
  if (err == ESP_ERR_NOT_FOUND) {
    ESP_LOGW(TAG, "Camera not detected; streaming disabled");
    return ESP_ERR_NOT_FOUND;
  }
  if (err != ESP_OK) {
    return err;
  }

  err = init_jpeg_encoder();
  if (err != ESP_OK) {
    return err;
  }

  s_streaming = true;

  err = start_http_server();
  if (err != ESP_OK) {
    s_streaming = false;
    return err;
  }

  char ip_addr[WIFI_REMOTE_MANAGER_IPV4_STR_LEN] = {0};
  if (wifi_remote_manager_get_sta_ip(ip_addr, sizeof(ip_addr)) == ESP_OK) {
    ESP_LOGI(TAG, "MJPEG stream available at http://%s:%d/stream",
             ip_addr, CONFIG_THEO_MJPEG_STREAM_PORT);
  } else {
    ESP_LOGI(TAG, "MJPEG stream available at http://<ip>:%d/stream",
             CONFIG_THEO_MJPEG_STREAM_PORT);
  }
  return ESP_OK;
}

void mjpeg_stream_stop(void)
{
  s_streaming = false;

  if (s_httpd) {
    httpd_stop(s_httpd);
    s_httpd = NULL;
  }

  if (s_cam_fd >= 0) {
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(s_cam_fd, VIDIOC_STREAMOFF, &type);

    for (int i = 0; i < CAM_BUF_COUNT; i++) {
      if (s_cam_buffers[i].start && s_cam_buffers[i].start != MAP_FAILED) {
        munmap(s_cam_buffers[i].start, s_cam_buffers[i].length);
      }
    }

    close(s_cam_fd);
    s_cam_fd = -1;
  }

  if (s_jpeg_fd >= 0) {
    enum v4l2_buf_type out_type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    enum v4l2_buf_type cap_type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(s_jpeg_fd, VIDIOC_STREAMOFF, &out_type);
    ioctl(s_jpeg_fd, VIDIOC_STREAMOFF, &cap_type);

    for (int i = 0; i < JPEG_BUF_COUNT; i++) {
      if (s_jpeg_out_buffers[i].start && s_jpeg_out_buffers[i].start != MAP_FAILED) {
        munmap(s_jpeg_out_buffers[i].start, s_jpeg_out_buffers[i].length);
      }
      if (s_jpeg_cap_buffers[i].start && s_jpeg_cap_buffers[i].start != MAP_FAILED) {
        munmap(s_jpeg_cap_buffers[i].start, s_jpeg_cap_buffers[i].length);
      }
    }

    close(s_jpeg_fd);
    s_jpeg_fd = -1;
  }

  if (s_video_initialized) {
    esp_video_deinit();
    s_video_initialized = false;
  }

  if (s_jpeg_handle) {
    jpeg_del_encoder_engine(s_jpeg_handle);
    s_jpeg_handle = NULL;
  }

  if (s_ldo_mipi_phy) {
    esp_ldo_release_channel(s_ldo_mipi_phy);
    s_ldo_mipi_phy = NULL;
  }

  ESP_LOGI(TAG, "MJPEG stream stopped");
}
