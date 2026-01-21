#include "h264_stream.h"

#include <fcntl.h>
#include <stdlib.h>
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
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "inttypes.h"
#include "linux/videodev2.h"
#include "sdkconfig.h"
#include "streaming_state.h"
#include "thermostat/ir_led.h"

static const char *TAG = "h264_stream";

static void log_internal_heap_state(const char *label, esp_log_level_t level, bool include_minimum)
{
  size_t free_bytes = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  size_t largest_block = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
  if (include_minimum)
  {
    size_t min_free = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
    ESP_LOG_LEVEL(level, TAG, "%s internal heap: free=%zu largest_block=%zu min_free=%zu",
                  label,
                  free_bytes,
                  largest_block,
                  min_free);
  }
  else
  {
    ESP_LOG_LEVEL(level, TAG, "%s internal heap: free=%zu largest_block=%zu",
                  label,
                  free_bytes,
                  largest_block);
  }
}

#define FRAME_WIDTH   800
#define FRAME_HEIGHT  800
#define CAM_BUF_COUNT 2
#define H264_BUF_COUNT 2
#define H264_FPS CONFIG_THEO_H264_STREAM_FPS
#define STREAM_MAX_OPEN_SOCKETS 2
#define HTTPD_INTERNAL_SOCKETS 3
#define STREAM_REQUIRED_LWIP_SOCKETS (STREAM_MAX_OPEN_SOCKETS + HTTPD_INTERNAL_SOCKETS)

#if defined(CONFIG_LWIP_MAX_SOCKETS) && CONFIG_LWIP_MAX_SOCKETS < STREAM_REQUIRED_LWIP_SOCKETS
#error "CONFIG_LWIP_MAX_SOCKETS must be >= STREAM_REQUIRED_LWIP_SOCKETS"
#endif

#define VIDEO_STREAM_TASK_STACK 8192
#define VIDEO_STREAM_TASK_PRIORITY 5

typedef struct {
  void *start;
  size_t length;
} buffer_t;

typedef struct {
  httpd_req_t *req;
} video_stream_context_t;

static esp_ldo_channel_handle_t s_ldo_mipi_phy = NULL;
static int s_cam_fd = -1;
static int s_h264_fd = -1;
static buffer_t s_cam_buffers[CAM_BUF_COUNT];
static buffer_t s_h264_out_buffers[H264_BUF_COUNT];
static buffer_t s_h264_cap_buffers[H264_BUF_COUNT];
static httpd_handle_t s_httpd = NULL;
static bool s_video_initialized = false;
static bool s_streams_active = false;
static TaskHandle_t s_drain_task_handle = NULL;

#define DRAIN_TASK_STACK 4096
#define DRAIN_TASK_PRIORITY 4

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

  struct v4l2_ext_controls flip_controls;
  struct v4l2_ext_control flip_ctrl[2];
  memset(&flip_controls, 0, sizeof(flip_controls));
  memset(flip_ctrl, 0, sizeof(flip_ctrl));
  flip_controls.ctrl_class = V4L2_CTRL_CLASS_USER;
  flip_controls.count = 2;
  flip_controls.controls = flip_ctrl;
  flip_ctrl[0].id = V4L2_CID_HFLIP;
  flip_ctrl[0].value = 1;
  flip_ctrl[1].id = V4L2_CID_VFLIP;
  flip_ctrl[1].value = 1;
  if (ioctl(s_cam_fd, VIDIOC_S_EXT_CTRLS, &flip_controls) != 0) {
    ESP_LOGW(TAG, "Failed to set camera flip controls");
  } else {
    ESP_LOGI(TAG, "Camera flip enabled (horizontal and vertical)");
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
  struct v4l2_ext_control control[4];
  memset(&controls, 0, sizeof(controls));
  memset(control, 0, sizeof(control));

  controls.ctrl_class = V4L2_CID_CODEC_CLASS;
  controls.count = 4;
  controls.controls = control;
  control[0].id = V4L2_CID_MPEG_VIDEO_H264_I_PERIOD;
  control[0].value = H264_FPS;
  control[1].id = V4L2_CID_MPEG_VIDEO_BITRATE;
  control[1].value = 1500000;
  control[2].id = V4L2_CID_MPEG_VIDEO_H264_MIN_QP;
  control[2].value = 18;
  control[3].id = V4L2_CID_MPEG_VIDEO_H264_MAX_QP;
  control[3].value = 35;
  if (ioctl(s_h264_fd, VIDIOC_S_EXT_CTRLS, &controls) != 0) {
    ESP_LOGW(TAG, "Failed to set H.264 encoder parameters");
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

  ESP_LOGI(TAG, "H.264 encoder initialized (i-period=%d)", H264_FPS);
  return ESP_OK;
}

static void stop_pipeline(void)
{
  bool pipeline_was_active = streaming_state_video_pipeline_active();
  streaming_state_set_video_pipeline_active(false);

  if (s_cam_fd >= 0) {
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(s_cam_fd, VIDIOC_STREAMOFF, &type);
  }

  for (int buffer_index = 0; buffer_index < CAM_BUF_COUNT; buffer_index++) {
    if (s_cam_buffers[buffer_index].start &&
        s_cam_buffers[buffer_index].start != MAP_FAILED) {
      munmap(s_cam_buffers[buffer_index].start,
             s_cam_buffers[buffer_index].length);
    }
    s_cam_buffers[buffer_index].start = NULL;
    s_cam_buffers[buffer_index].length = 0;
  }

  if (s_cam_fd >= 0) {
    close(s_cam_fd);
    s_cam_fd = -1;
  }

  if (s_h264_fd >= 0) {
    enum v4l2_buf_type out_type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    enum v4l2_buf_type cap_type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(s_h264_fd, VIDIOC_STREAMOFF, &out_type);
    ioctl(s_h264_fd, VIDIOC_STREAMOFF, &cap_type);
  }

  for (int buffer_index = 0; buffer_index < H264_BUF_COUNT; buffer_index++) {
    if (s_h264_out_buffers[buffer_index].start &&
        s_h264_out_buffers[buffer_index].start != MAP_FAILED) {
      munmap(s_h264_out_buffers[buffer_index].start,
             s_h264_out_buffers[buffer_index].length);
    }
    if (s_h264_cap_buffers[buffer_index].start &&
        s_h264_cap_buffers[buffer_index].start != MAP_FAILED) {
      munmap(s_h264_cap_buffers[buffer_index].start,
             s_h264_cap_buffers[buffer_index].length);
    }
    s_h264_out_buffers[buffer_index].start = NULL;
    s_h264_out_buffers[buffer_index].length = 0;
    s_h264_cap_buffers[buffer_index].start = NULL;
    s_h264_cap_buffers[buffer_index].length = 0;
  }

  if (s_h264_fd >= 0) {
    close(s_h264_fd);
    s_h264_fd = -1;
  }

  if (s_video_initialized) {
    ESP_LOGI(TAG, "Video subsystem retained for reconnect");
  } else {
    ESP_LOGI(TAG, "Video subsystem not initialized; nothing to retain");
  }

  if (s_ldo_mipi_phy) {
    esp_ldo_release_channel(s_ldo_mipi_phy);
    s_ldo_mipi_phy = NULL;
  }

  if (pipeline_was_active) {
    ESP_LOGI(TAG, "H.264 pipeline stopped");
  }

  log_internal_heap_state("After H.264 pipeline stop", ESP_LOG_INFO, false);
}

static esp_err_t start_pipeline(void)
{
  ESP_LOGI(TAG, "H.264 pipeline init: enable LDO");
  esp_err_t err = init_ldo();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "H.264 pipeline init failed (LDO): %s", esp_err_to_name(err));
    stop_pipeline();
    streaming_state_set_video_failed(true);
    return err;
  }

  if (!s_video_initialized) {
    ESP_LOGI(TAG, "H.264 pipeline init: esp_video");
    err = init_video_subsystem();
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "H.264 pipeline init failed (esp_video): %s",
               esp_err_to_name(err));
      stop_pipeline();
      streaming_state_set_video_failed(true);
      return err;
    }
  } else {
    ESP_LOGI(TAG, "H.264 pipeline init: esp_video already initialized");
  }

  ESP_LOGI(TAG, "H.264 pipeline init: V4L2 capture");
  err = init_v4l2_capture();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "H.264 pipeline init failed (capture): %s", esp_err_to_name(err));
    stop_pipeline();
    streaming_state_set_video_failed(true);
    return err;
  }

  ESP_LOGI(TAG, "H.264 pipeline init: H.264 encoder");
  log_internal_heap_state("Before H.264 encoder", ESP_LOG_INFO, false);
  err = init_h264_encoder();
  if (err != ESP_OK) {
    log_internal_heap_state("H.264 encoder init failed", ESP_LOG_WARN, true);
    ESP_LOGE(TAG, "H.264 pipeline init failed (encoder): %s", esp_err_to_name(err));
    stop_pipeline();
    streaming_state_set_video_failed(true);
    return err;
  }

  streaming_state_set_video_pipeline_active(true);
  ESP_LOGI(TAG, "H.264 pipeline started");
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

static void video_stream_task(void *context)
{
  video_stream_context_t *stream_context = (video_stream_context_t *)context;
  httpd_req_t *req = stream_context->req;
  free(stream_context);

  if (!streaming_state_lock(portMAX_DELAY)) {
    ESP_LOGE(TAG, "Streaming state not initialized");
    httpd_resp_set_status(req, "500 Internal Server Error");
    httpd_resp_send(req, NULL, 0);
    httpd_req_async_handler_complete(req);
    vTaskDelete(NULL);
    return;
  }

  if (!streaming_state_video_pipeline_active()) {
    esp_err_t err = start_pipeline();
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "H.264 pipeline start failed: %s", esp_err_to_name(err));
      streaming_state_set_video_client_active(false);
      streaming_state_decrement_refcount();
      streaming_state_set_video_failed(true);
      streaming_state_unlock();
      httpd_resp_set_status(req, "503 Service Unavailable");
      httpd_resp_send(req, NULL, 0);
      httpd_req_async_handler_complete(req);
      vTaskDelete(NULL);
      return;
    }
  }

  esp_err_t ir_err = thermostat_ir_led_init();
  if (ir_err == ESP_OK) {
    thermostat_ir_led_set(true);
  } else {
    ESP_LOGW(TAG, "IR LED init failed: %s", esp_err_to_name(ir_err));
  }

  streaming_state_unlock();

  esp_err_t err = httpd_resp_set_type(req, "video/h264");
  if (err != ESP_OK) {
    if (streaming_state_lock(portMAX_DELAY)) {
      streaming_state_set_video_client_active(false);
      streaming_state_decrement_refcount();
      thermostat_ir_led_set(false);
      stop_pipeline();
      streaming_state_unlock();
    } else {
      thermostat_ir_led_set(false);
      stop_pipeline();
    }
    httpd_req_async_handler_complete(req);
    vTaskDelete(NULL);
    return;
  }

  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_hdr(req, "Cache-Control", "no-cache");

  // Start the pipeline streams after HTTP headers are set
  enum v4l2_buf_type cam_type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if (ioctl(s_cam_fd, VIDIOC_STREAMON, &cam_type) < 0) {
    ESP_LOGE(TAG, "Failed to start camera capture stream");
    if (streaming_state_lock(portMAX_DELAY)) {
      streaming_state_set_video_client_active(false);
      streaming_state_decrement_refcount();
      thermostat_ir_led_set(false);
      stop_pipeline();
      streaming_state_unlock();
    } else {
      thermostat_ir_led_set(false);
      stop_pipeline();
    }
    httpd_req_async_handler_complete(req);
    vTaskDelete(NULL);
    return;
  }

  enum v4l2_buf_type enc_out_type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
  if (ioctl(s_h264_fd, VIDIOC_STREAMON, &enc_out_type) < 0) {
    ESP_LOGE(TAG, "Failed to start H.264 output stream");
    enum v4l2_buf_type stop_type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(s_cam_fd, VIDIOC_STREAMOFF, &stop_type);
    if (streaming_state_lock(portMAX_DELAY)) {
      streaming_state_set_video_client_active(false);
      streaming_state_decrement_refcount();
      thermostat_ir_led_set(false);
      stop_pipeline();
      streaming_state_unlock();
    } else {
      thermostat_ir_led_set(false);
      stop_pipeline();
    }
    httpd_req_async_handler_complete(req);
    vTaskDelete(NULL);
    return;
  }

  enum v4l2_buf_type enc_cap_type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if (ioctl(s_h264_fd, VIDIOC_STREAMON, &enc_cap_type) < 0) {
    ESP_LOGE(TAG, "Failed to start H.264 capture stream");
    enum v4l2_buf_type stop_type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(s_cam_fd, VIDIOC_STREAMOFF, &stop_type);
    stop_type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    ioctl(s_h264_fd, VIDIOC_STREAMOFF, &stop_type);
    if (streaming_state_lock(portMAX_DELAY)) {
      streaming_state_set_video_client_active(false);
      streaming_state_decrement_refcount();
      thermostat_ir_led_set(false);
      stop_pipeline();
      streaming_state_unlock();
    } else {
      thermostat_ir_led_set(false);
      stop_pipeline();
    }
    httpd_req_async_handler_complete(req);
    vTaskDelete(NULL);
    return;
  }

  ESP_LOGI(TAG, "Video client connected");

  const size_t expected_yuv_bytes =
      (size_t)FRAME_WIDTH * (size_t)FRAME_HEIGHT * 3 / 2;
  bool logged_first_frame = false;
  bool disconnected_logged = false;

  while (true) {
    if (!streaming_state_lock(pdMS_TO_TICKS(100))) {
      ESP_LOGE(TAG, "Failed to acquire streaming state");
      break;
    }
    bool video_active = streaming_state_video_client_active();
    streaming_state_unlock();
    if (!video_active) {
      break;
    }

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
      ESP_LOGI(TAG, "Video client disconnected");
      disconnected_logged = true;
      break;
    }
  }

  if (!disconnected_logged) {
    ESP_LOGI(TAG, "Video client disconnected");
  }

  if (streaming_state_lock(portMAX_DELAY)) {
    streaming_state_set_video_client_active(false);
    streaming_state_decrement_refcount();
    thermostat_ir_led_set(false);
    stop_pipeline();
    streaming_state_unlock();
  } else {
    thermostat_ir_led_set(false);
    stop_pipeline();
  }

  httpd_resp_send_chunk(req, NULL, 0);
  httpd_req_async_handler_complete(req);
  vTaskDelete(NULL);
}

static esp_err_t stream_handler(httpd_req_t *req)
{
  if (!streaming_state_lock(portMAX_DELAY)) {
    ESP_LOGE(TAG, "Streaming state not initialized");
    return ESP_ERR_INVALID_STATE;
  }

  if (streaming_state_video_failed() || streaming_state_video_client_active()) {
    ESP_LOGW(TAG, "Video client rejected (already active or failed)");
    streaming_state_unlock();
    httpd_resp_set_status(req, "503 Service Unavailable");
    return httpd_resp_send(req, NULL, 0);
  }

  streaming_state_set_video_client_active(true);
  streaming_state_increment_refcount();
  streaming_state_unlock();

  httpd_req_t *async_req = NULL;
  esp_err_t err = httpd_req_async_handler_begin(req, &async_req);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to start async handler: %s", esp_err_to_name(err));
    if (streaming_state_lock(portMAX_DELAY)) {
      streaming_state_set_video_client_active(false);
      streaming_state_decrement_refcount();
      streaming_state_unlock();
    }
    httpd_resp_set_status(req, "500 Internal Server Error");
    return httpd_resp_send(req, NULL, 0);
  }

  video_stream_context_t *stream_context = heap_caps_calloc(1,
                                                            sizeof(*stream_context),
                                                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (stream_context == NULL) {
    ESP_LOGE(TAG, "Failed to allocate stream context");
    if (streaming_state_lock(portMAX_DELAY)) {
      streaming_state_set_video_client_active(false);
      streaming_state_decrement_refcount();
      streaming_state_unlock();
    }
    httpd_resp_set_status(async_req, "500 Internal Server Error");
    httpd_resp_send(async_req, NULL, 0);
    httpd_req_async_handler_complete(async_req);
    return ESP_OK;
  }

  stream_context->req = async_req;

  if (xTaskCreatePinnedToCoreWithCaps(video_stream_task,
                  "video_stream",
                  VIDEO_STREAM_TASK_STACK,
                  stream_context,
                  VIDEO_STREAM_TASK_PRIORITY,
                  NULL,
                  tskNO_AFFINITY,
                  MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
    ESP_LOGE(TAG, "Failed to start video stream task");
    free(stream_context);
    if (streaming_state_lock(portMAX_DELAY)) {
      streaming_state_set_video_client_active(false);
      streaming_state_decrement_refcount();
      streaming_state_unlock();
    }
    httpd_resp_set_status(async_req, "500 Internal Server Error");
    httpd_resp_send(async_req, NULL, 0);
    httpd_req_async_handler_complete(async_req);
    return ESP_OK;
  }

  return ESP_OK;
}

static esp_err_t start_http_server(void)
{
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = CONFIG_THEO_H264_STREAM_PORT;
  config.stack_size = 8192;
  config.core_id = 1;
  config.max_open_sockets = STREAM_MAX_OPEN_SOCKETS;
  config.task_priority = 6;
  config.task_caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;

  esp_err_t err = httpd_start(&s_httpd, &config);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(err));
    return err;
  }

  httpd_uri_t stream_uri = {
    .uri = "/video",
    .method = HTTP_GET,
    .handler = stream_handler,
    .user_ctx = NULL,
  };

  err = httpd_register_uri_handler(s_httpd, &stream_uri);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to register video handler: %s", esp_err_to_name(err));
    httpd_stop(s_httpd);
    s_httpd = NULL;
    return err;
  }

  ESP_LOGI(TAG, "HTTP server started on port %d", CONFIG_THEO_H264_STREAM_PORT);
  return ESP_OK;
}

esp_err_t h264_stream_start(void)
{
  ESP_LOGI(TAG, "Starting H.264 stream server...");

  esp_err_t err = streaming_state_init();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize streaming state: %s", esp_err_to_name(err));
    return err;
  }

  err = start_http_server();
  if (err != ESP_OK) {
    streaming_state_deinit();
    return err;
  }

  char ip_addr[WIFI_REMOTE_MANAGER_IPV4_STR_LEN] = {0};
  if (wifi_remote_manager_get_sta_ip(ip_addr, sizeof(ip_addr)) == ESP_OK) {
    ESP_LOGI(TAG, "H.264 stream available at http://%s:%d/video",
             ip_addr, CONFIG_THEO_H264_STREAM_PORT);
  } else {
    ESP_LOGI(TAG, "H.264 stream available at http://<ip>:%d/video",
             CONFIG_THEO_H264_STREAM_PORT);
  }
  return ESP_OK;
}

void h264_stream_stop(void)
{
  if (streaming_state_lock(portMAX_DELAY)) {
    streaming_state_set_video_client_active(false);
    streaming_state_unlock();
  }

  if (s_httpd) {
    httpd_stop(s_httpd);
    s_httpd = NULL;
  }

  if (streaming_state_lock(portMAX_DELAY)) {
    thermostat_ir_led_set(false);
    if (streaming_state_video_pipeline_active()) {
      stop_pipeline();
    }
    streaming_state_unlock();
  } else {
    thermostat_ir_led_set(false);
    stop_pipeline();
  }

  streaming_state_deinit();

  ESP_LOGI(TAG, "H.264 stream stopped");
}
