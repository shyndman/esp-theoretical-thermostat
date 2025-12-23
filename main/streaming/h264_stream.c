#include "h264_stream.h"

#include <fcntl.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "bsp/esp32_p4_nano.h"
#include "connectivity/wifi_remote_manager.h"
#include "esp_err.h"
#include "esp_http_server.h"
#include "esp_ldo_regulator.h"
#include "esp_log.h"
#include "esp_video_init.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "inttypes.h"
#include "linux/videodev2.h"
#include "sdkconfig.h"

static const char *TAG = "h264_stream";

#define FRAME_WIDTH   800
#define FRAME_HEIGHT  800
#define CAM_BUF_COUNT 2
#define H264_BUF_COUNT 2
#define H264_FPS CONFIG_THEO_H264_STREAM_FPS

typedef struct {
  void *start;
  size_t length;
} buffer_t;

static esp_ldo_channel_handle_t s_ldo_mipi_phy = NULL;
static int s_cam_fd = -1;
static int s_h264_fd = -1;
static buffer_t s_cam_buffers[CAM_BUF_COUNT];
static buffer_t s_h264_out_buffers[H264_BUF_COUNT];
static buffer_t s_h264_cap_buffers[H264_BUF_COUNT];
static httpd_handle_t s_httpd = NULL;
static volatile bool s_streaming = false;
static bool s_video_initialized = false;

static void fourcc_to_str(uint32_t fourcc, char out[5])
{
  out[0] = (char)(fourcc & 0xFF);
  out[1] = (char)((fourcc >> 8) & 0xFF);
  out[2] = (char)((fourcc >> 16) & 0xFF);
  out[3] = (char)((fourcc >> 24) & 0xFF);
  out[4] = '\0';
}

static void log_capture_formats(int fd)
{
  ESP_LOGW(TAG, "Enumerating capture formats and frame sizes...");

  for (uint32_t fmt_index = 0;; fmt_index++) {
    struct v4l2_fmtdesc fmtdesc = {
      .index = fmt_index,
      .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
    };

    if (ioctl(fd, VIDIOC_ENUM_FMT, &fmtdesc) != 0) {
      break;
    }

    char fourcc[5];
    fourcc_to_str(fmtdesc.pixelformat, fourcc);
    ESP_LOGW(TAG, "Capture format %u: %s (%s)", fmt_index, fourcc, (char *)fmtdesc.description);

    for (uint32_t size_index = 0;; size_index++) {
      struct v4l2_frmsizeenum frmsize = {
        .index = size_index,
        .pixel_format = fmtdesc.pixelformat,
      };

      if (ioctl(fd, VIDIOC_ENUM_FRAMESIZES, &frmsize) != 0) {
        break;
      }

      if (frmsize.type == V4L2_FRMSIZE_TYPE_DISCRETE) {
        ESP_LOGW(TAG, "  size %u: %ux%u", size_index,
                 frmsize.discrete.width, frmsize.discrete.height);
      } else if (frmsize.type == V4L2_FRMSIZE_TYPE_STEPWISE) {
        ESP_LOGW(TAG,
                 "  size %u: stepwise min=%ux%u max=%ux%u step=%ux%u",
                 size_index,
                 frmsize.stepwise.min_width, frmsize.stepwise.min_height,
                 frmsize.stepwise.max_width, frmsize.stepwise.max_height,
                 frmsize.stepwise.step_width, frmsize.stepwise.step_height);
      } else if (frmsize.type == V4L2_FRMSIZE_TYPE_CONTINUOUS) {
        ESP_LOGW(TAG,
                 "  size %u: continuous min=%ux%u max=%ux%u",
                 size_index,
                 frmsize.stepwise.min_width, frmsize.stepwise.min_height,
                 frmsize.stepwise.max_width, frmsize.stepwise.max_height);
      }
    }
  }
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
  i2c_master_bus_handle_t i2c_handle = bsp_i2c_get_handle();
  if (i2c_handle == NULL) {
    ESP_LOGE(TAG, "bsp_i2c_get_handle returned NULL");
    return ESP_ERR_INVALID_STATE;
  }

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
  };

  esp_err_t err = esp_video_init(&video_cfg);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to init esp_video: %s", esp_err_to_name(err));
    return err;
  }

  s_video_initialized = true;
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
      .pixelformat = V4L2_PIX_FMT_YUV420,
    },
  };

  if (ioctl(s_cam_fd, VIDIOC_S_FMT, &fmt) < 0) {
    ESP_LOGE(TAG, "Failed to set capture format");
    log_capture_formats(s_cam_fd);
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
    if (active_fmt.fmt.pix.pixelformat != V4L2_PIX_FMT_YUV420) {
      ESP_LOGE(TAG, "Capture did not provide YUV420; H.264 encoder requires YUV420");
      return ESP_ERR_NOT_SUPPORTED;
    }
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

  ESP_LOGI(TAG, "V4L2 capture initialized (%dx%d YUV420)", FRAME_WIDTH, FRAME_HEIGHT);
  return ESP_OK;
}

static esp_err_t init_h264_encoder(void)
{
  s_h264_fd = open("/dev/video11", O_RDWR);
  if (s_h264_fd < 0) {
    ESP_LOGE(TAG, "Failed to open /dev/video11 (H.264 encoder)");
    return ESP_FAIL;
  }

  // Configure output format (input to encoder: YUV420)
  struct v4l2_format out_fmt = {
    .type = V4L2_BUF_TYPE_VIDEO_OUTPUT,
    .fmt.pix = {
      .width = FRAME_WIDTH,
      .height = FRAME_HEIGHT,
      .pixelformat = V4L2_PIX_FMT_YUV420,
    },
  };

  if (ioctl(s_h264_fd, VIDIOC_S_FMT, &out_fmt) < 0) {
    ESP_LOGE(TAG, "Failed to set H.264 encoder input format");
    return ESP_FAIL;
  }

  struct v4l2_format active_out_fmt = {
    .type = V4L2_BUF_TYPE_VIDEO_OUTPUT,
  };
  if (ioctl(s_h264_fd, VIDIOC_G_FMT, &active_out_fmt) == 0) {
    char requested_fourcc[5];
    char active_fourcc[5];
    fourcc_to_str(out_fmt.fmt.pix.pixelformat, requested_fourcc);
    fourcc_to_str(active_out_fmt.fmt.pix.pixelformat, active_fourcc);
    ESP_LOGI(TAG,
             "H.264 input format: requested=%ux%u %s, active=%ux%u %s (bytesperline=%u sizeimage=%u)",
             out_fmt.fmt.pix.width, out_fmt.fmt.pix.height, requested_fourcc,
             active_out_fmt.fmt.pix.width, active_out_fmt.fmt.pix.height, active_fourcc,
             active_out_fmt.fmt.pix.bytesperline, active_out_fmt.fmt.pix.sizeimage);
  } else {
    ESP_LOGW(TAG, "Failed to query active H.264 input format");
  }

  // Configure capture format (output from encoder: H.264)
  struct v4l2_format cap_fmt = {
    .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
    .fmt.pix = {
      .width = FRAME_WIDTH,
      .height = FRAME_HEIGHT,
      .pixelformat = V4L2_PIX_FMT_H264,
    },
  };

  if (ioctl(s_h264_fd, VIDIOC_S_FMT, &cap_fmt) < 0) {
    ESP_LOGE(TAG, "Failed to set H.264 encoder output format");
    return ESP_FAIL;
  }

  struct v4l2_format active_cap_fmt = {
    .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
  };
  if (ioctl(s_h264_fd, VIDIOC_G_FMT, &active_cap_fmt) == 0) {
    char requested_fourcc[5];
    char active_fourcc[5];
    fourcc_to_str(cap_fmt.fmt.pix.pixelformat, requested_fourcc);
    fourcc_to_str(active_cap_fmt.fmt.pix.pixelformat, active_fourcc);
    ESP_LOGI(TAG,
             "H.264 output format: requested=%ux%u %s, active=%ux%u %s (bytesperline=%u sizeimage=%u)",
             cap_fmt.fmt.pix.width, cap_fmt.fmt.pix.height, requested_fourcc,
             active_cap_fmt.fmt.pix.width, active_cap_fmt.fmt.pix.height, active_fourcc,
             active_cap_fmt.fmt.pix.bytesperline, active_cap_fmt.fmt.pix.sizeimage);
  } else {
    ESP_LOGW(TAG, "Failed to query active H.264 output format");
  }

  struct v4l2_ext_controls controls;
  struct v4l2_ext_control control[1];
  memset(&controls, 0, sizeof(controls));
  memset(control, 0, sizeof(control));

  controls.ctrl_class = V4L2_CID_CODEC_CLASS;
  controls.count = 1;
  controls.controls = control;
  control[0].id = V4L2_CID_MPEG_VIDEO_H264_I_PERIOD;
  control[0].value = H264_FPS;
  if (ioctl(s_h264_fd, VIDIOC_S_EXT_CTRLS, &controls) != 0) {
    ESP_LOGW(TAG, "Failed to set H.264 intra frame period");
  }

  // Request output buffers (input to encoder)
  struct v4l2_requestbuffers out_req = {
    .count = H264_BUF_COUNT,
    .type = V4L2_BUF_TYPE_VIDEO_OUTPUT,
    .memory = V4L2_MEMORY_MMAP,
  };

  if (ioctl(s_h264_fd, VIDIOC_REQBUFS, &out_req) < 0) {
    ESP_LOGE(TAG, "Failed to request H.264 output buffers");
    return ESP_FAIL;
  }

  // Request capture buffers (output from encoder)
  struct v4l2_requestbuffers cap_req = {
    .count = H264_BUF_COUNT,
    .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
    .memory = V4L2_MEMORY_MMAP,
  };

  if (ioctl(s_h264_fd, VIDIOC_REQBUFS, &cap_req) < 0) {
    ESP_LOGE(TAG, "Failed to request H.264 capture buffers");
    return ESP_FAIL;
  }

  // Map output buffers
  for (int i = 0; i < H264_BUF_COUNT; i++) {
    struct v4l2_buffer buf = {
      .type = V4L2_BUF_TYPE_VIDEO_OUTPUT,
      .memory = V4L2_MEMORY_MMAP,
      .index = i,
    };

    if (ioctl(s_h264_fd, VIDIOC_QUERYBUF, &buf) < 0) {
      ESP_LOGE(TAG, "Failed to query H.264 output buffer %d", i);
      return ESP_FAIL;
    }

    s_h264_out_buffers[i].length = buf.length;
    s_h264_out_buffers[i].start = mmap(NULL, buf.length, PROT_READ | PROT_WRITE,
                                        MAP_SHARED, s_h264_fd, buf.m.offset);
    if (s_h264_out_buffers[i].start == MAP_FAILED) {
      ESP_LOGE(TAG, "Failed to mmap H.264 output buffer %d", i);
      return ESP_FAIL;
    }
  }

  // Map capture buffers
  for (int i = 0; i < H264_BUF_COUNT; i++) {
    struct v4l2_buffer buf = {
      .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
      .memory = V4L2_MEMORY_MMAP,
      .index = i,
    };

    if (ioctl(s_h264_fd, VIDIOC_QUERYBUF, &buf) < 0) {
      ESP_LOGE(TAG, "Failed to query H.264 capture buffer %d", i);
      return ESP_FAIL;
    }

    s_h264_cap_buffers[i].length = buf.length;
    s_h264_cap_buffers[i].start = mmap(NULL, buf.length, PROT_READ | PROT_WRITE,
                                        MAP_SHARED, s_h264_fd, buf.m.offset);
    if (s_h264_cap_buffers[i].start == MAP_FAILED) {
      ESP_LOGE(TAG, "Failed to mmap H.264 capture buffer %d", i);
      return ESP_FAIL;
    }

    // Pre-queue capture buffers
    if (ioctl(s_h264_fd, VIDIOC_QBUF, &buf) < 0) {
      ESP_LOGE(TAG, "Failed to queue H.264 capture buffer %d", i);
      return ESP_FAIL;
    }
  }

  // Start encoder streams
  enum v4l2_buf_type out_type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
  enum v4l2_buf_type cap_type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

  if (ioctl(s_h264_fd, VIDIOC_STREAMON, &out_type) < 0) {
    ESP_LOGE(TAG, "Failed to start H.264 output stream");
    return ESP_FAIL;
  }

  if (ioctl(s_h264_fd, VIDIOC_STREAMON, &cap_type) < 0) {
    ESP_LOGE(TAG, "Failed to start H.264 capture stream");
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "H.264 encoder initialized (i-period=%d)", H264_FPS);
  return ESP_OK;
}

static esp_err_t encode_frame_to_h264(void *yuv_data, size_t yuv_size,
                                      void **h264_data, size_t *h264_size)
{
  static int buf_idx = 0;

  struct v4l2_buffer out_buf = {
    .type = V4L2_BUF_TYPE_VIDEO_OUTPUT,
    .memory = V4L2_MEMORY_MMAP,
    .index = buf_idx,
  };

  memcpy(s_h264_out_buffers[buf_idx].start, yuv_data, yuv_size);
  out_buf.bytesused = yuv_size;

  if (ioctl(s_h264_fd, VIDIOC_QBUF, &out_buf) < 0) {
    ESP_LOGE(TAG, "Failed to queue frame for encoding");
    return ESP_FAIL;
  }

  struct v4l2_buffer cap_buf = {
    .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
    .memory = V4L2_MEMORY_MMAP,
  };

  if (ioctl(s_h264_fd, VIDIOC_DQBUF, &cap_buf) < 0) {
    ESP_LOGE(TAG, "Failed to dequeue encoded frame");
    return ESP_FAIL;
  }

  *h264_data = s_h264_cap_buffers[cap_buf.index].start;
  *h264_size = cap_buf.bytesused;

  if (ioctl(s_h264_fd, VIDIOC_DQBUF, &out_buf) < 0) {
    ESP_LOGE(TAG, "Failed to dequeue encoder input buffer");
    return ESP_FAIL;
  }

  if (ioctl(s_h264_fd, VIDIOC_QBUF, &cap_buf) < 0) {
    ESP_LOGE(TAG, "Failed to re-queue capture buffer");
    return ESP_FAIL;
  }

  buf_idx = (buf_idx + 1) % H264_BUF_COUNT;
  return ESP_OK;
}

static esp_err_t stream_handler(httpd_req_t *req)
{
  ESP_LOGI(TAG, "Stream client connected");

  esp_err_t err = httpd_resp_set_type(req, "video/h264");
  if (err != ESP_OK) {
    return err;
  }

  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_hdr(req, "Cache-Control", "no-cache");

  const int frame_delay_ms = 1000 / H264_FPS;
  const size_t expected_yuv_bytes =
      (size_t)FRAME_WIDTH * (size_t)FRAME_HEIGHT * 3 / 2;
  bool logged_first_frame = false;

  while (s_streaming) {
    struct v4l2_buffer cam_buf = {
      .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
      .memory = V4L2_MEMORY_MMAP,
    };

    if (ioctl(s_cam_fd, VIDIOC_DQBUF, &cam_buf) < 0) {
      ESP_LOGE(TAG, "Failed to dequeue camera frame");
      break;
    }

    if (!logged_first_frame) {
      ESP_LOGI(TAG, "First frame: bytesused=%" PRIu32 " expected=%zu",
               cam_buf.bytesused, expected_yuv_bytes);
      logged_first_frame = true;
    }

    void *h264_data = NULL;
    size_t h264_size = 0;
    err = encode_frame_to_h264(s_cam_buffers[cam_buf.index].start,
                               cam_buf.bytesused, &h264_data, &h264_size);

    if (ioctl(s_cam_fd, VIDIOC_QBUF, &cam_buf) < 0) {
      ESP_LOGE(TAG, "Failed to re-queue camera buffer");
      break;
    }

    if (err != ESP_OK || h264_data == NULL) {
      ESP_LOGE(TAG, "Frame encoding failed");
      continue;
    }

    if (httpd_resp_send_chunk(req, h264_data, h264_size) != ESP_OK) {
      ESP_LOGI(TAG, "Stream client disconnected");
      break;
    }

    vTaskDelay(pdMS_TO_TICKS(frame_delay_ms));
  }

  return ESP_OK;
}

static esp_err_t start_http_server(void)
{
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = CONFIG_THEO_H264_STREAM_PORT;
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

  ESP_LOGI(TAG, "HTTP server started on port %d", CONFIG_THEO_H264_STREAM_PORT);
  return ESP_OK;
}

esp_err_t h264_stream_start(void)
{
  esp_err_t err;

  ESP_LOGI(TAG, "Starting H.264 stream server...");

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

  err = init_h264_encoder();
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
    ESP_LOGI(TAG, "H.264 stream available at http://%s:%d/stream",
             ip_addr, CONFIG_THEO_H264_STREAM_PORT);
  } else {
    ESP_LOGI(TAG, "H.264 stream available at http://<ip>:%d/stream",
             CONFIG_THEO_H264_STREAM_PORT);
  }
  return ESP_OK;
}

void h264_stream_stop(void)
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

  if (s_h264_fd >= 0) {
    enum v4l2_buf_type out_type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    enum v4l2_buf_type cap_type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(s_h264_fd, VIDIOC_STREAMOFF, &out_type);
    ioctl(s_h264_fd, VIDIOC_STREAMOFF, &cap_type);

    for (int i = 0; i < H264_BUF_COUNT; i++) {
      if (s_h264_out_buffers[i].start && s_h264_out_buffers[i].start != MAP_FAILED) {
        munmap(s_h264_out_buffers[i].start, s_h264_out_buffers[i].length);
      }
      if (s_h264_cap_buffers[i].start && s_h264_cap_buffers[i].start != MAP_FAILED) {
        munmap(s_h264_cap_buffers[i].start, s_h264_cap_buffers[i].length);
      }
    }

    close(s_h264_fd);
    s_h264_fd = -1;
  }

  if (s_video_initialized) {
    esp_video_deinit();
    s_video_initialized = false;
  }

  if (s_ldo_mipi_phy) {
    esp_ldo_release_channel(s_ldo_mipi_phy);
    s_ldo_mipi_phy = NULL;
  }

  ESP_LOGI(TAG, "H.264 stream stopped");
}
