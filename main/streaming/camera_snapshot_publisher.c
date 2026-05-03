#include "streaming/camera_snapshot_publisher.h"

#if CONFIG_THEO_CAMERA_ENABLE

#include <errno.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "driver/i2c_master.h"
#include "driver/jpeg_encode.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_ldo_regulator.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_video_device.h"
#include "esp_video_init.h"
#include "mqtt_client.h"
#include "sdkconfig.h"

#include "bsp/esp32_p4_nano.h"
#include "connectivity/device_identity.h"
#include "connectivity/mqtt_manager.h"
#include "thermostat/ir_led.h"

#define TAG "camera_snapshot"

#define CAMERA_SNAPSHOT_WIDTH 800
#define CAMERA_SNAPSHOT_HEIGHT 800
#define CAMERA_SNAPSHOT_BUFFER_COUNT 2
#define CAMERA_SNAPSHOT_CAPTURE_TIMEOUT_MS 1000
#define CAMERA_SNAPSHOT_JPEG_BUFFER_BYTES (512 * 1024)
#define CAMERA_SNAPSHOT_JPEG_QUALITY 80
#define CAMERA_SNAPSHOT_METRICS_BATCH_SIZE 30
#define CAMERA_SNAPSHOT_STARTUP_SKIP_FRAMES 3
#define CAMERA_SNAPSHOT_TASK_PRIORITY 4
#define CAMERA_SNAPSHOT_TASK_STACK_BYTES 8192
#define CAMERA_SNAPSHOT_TOPIC_MAX_LEN 160
#define CAMERA_SNAPSHOT_TOPIC_SUFFIX "/camera/snapshot"
#define CAMERA_SNAPSHOT_AE_TARGET 64
#define CAMERA_SNAPSHOT_RED_BALANCE 800
#define CAMERA_SNAPSHOT_BLUE_BALANCE 1600

typedef struct {
  void *start;
  size_t length;
} camera_mmap_buffer_t;

static TaskHandle_t s_task_handle;
static bool s_started;
static bool s_stop_requested;
static esp_ldo_channel_handle_t s_ldo_mipi_phy;
static bool s_video_initialized;
static int s_camera_fd = -1;
static bool s_camera_streaming;
static uint32_t s_camera_pixfmt;
static size_t s_camera_row_stride;
static size_t s_frame_size_bytes;
static size_t s_camera_buffer_count;
static camera_mmap_buffer_t s_camera_buffers[CAMERA_SNAPSHOT_BUFFER_COUNT];
static jpeg_encoder_handle_t s_jpeg_encoder;
static uint8_t *s_jpeg_buffer;
static size_t s_jpeg_buffer_size;
static uint8_t *s_raw_buffer;
static size_t s_raw_buffer_size;
static char s_snapshot_topic[CAMERA_SNAPSHOT_TOPIC_MAX_LEN];
static bool s_ir_led_enabled;
static portMUX_TYPE s_state_lock = portMUX_INITIALIZER_UNLOCKED;

static void camera_snapshot_task(void *arg);
static esp_err_t build_snapshot_topic(void);
static esp_err_t acquire_mipi_phy_ldo(void);
static void release_mipi_phy_ldo(void);
static esp_err_t init_video_framework(void);
static void deinit_video_framework(void);
static esp_err_t open_camera_device(void);
static void close_camera_device(void);
static esp_err_t select_camera_format(void);
static esp_err_t apply_camera_tuning_controls(void);
static esp_err_t apply_isp_balance_controls(void);
static esp_err_t set_video_control(int fd, uint32_t ctrl_class, uint32_t id, int32_t value, const char *name);
static esp_err_t allocate_camera_buffers(void);
static esp_err_t start_camera_stream(void);
static void stop_camera_stream(void);
static esp_err_t skip_startup_frames(void);
static esp_err_t ensure_jpeg_encoder(void);
static void release_jpeg_encoder(void);
static esp_err_t ensure_jpeg_buffer(void);
static void release_jpeg_buffer(void);
static esp_err_t ensure_raw_buffer(void);
static void release_raw_buffer(void);
static size_t packed_frame_bytes(uint32_t pixfmt);
static jpeg_enc_input_format_t jpeg_src_type_for_pixfmt(uint32_t pixfmt);
static jpeg_down_sampling_type_t jpeg_subsample_for_pixfmt(uint32_t pixfmt);
static void copy_frame_to_jpeg_input(const uint8_t *frame,
                                     size_t source_stride,
                                     size_t bytes_per_pixel);
static esp_err_t encode_snapshot_frame(size_t *jpeg_size);
static void release_resources(void);
static bool stop_requested(void);
static void set_started_state(bool started);
static void log_publish_metrics(uint32_t publish_count,
                                uint32_t publish_failures,
                                uint64_t total_publish_time_us,
                                uint64_t max_publish_time_us,
                                uint64_t total_publish_bytes);
static const char *pixfmt_name(uint32_t pixfmt);

esp_err_t camera_snapshot_publisher_start(void)
{
  taskENTER_CRITICAL(&s_state_lock);
  if (s_started) {
    taskEXIT_CRITICAL(&s_state_lock);
    return ESP_OK;
  }
  s_started = true;
  s_stop_requested = false;
  taskEXIT_CRITICAL(&s_state_lock);

  esp_err_t err = build_snapshot_topic();
  if (err != ESP_OK) {
    set_started_state(false);
    return err;
  }

  BaseType_t task_ok = xTaskCreatePinnedToCoreWithCaps(camera_snapshot_task,
                                                       "cam_snapshot",
                                                       CAMERA_SNAPSHOT_TASK_STACK_BYTES,
                                                       NULL,
                                                       CAMERA_SNAPSHOT_TASK_PRIORITY,
                                                       &s_task_handle,
                                                       tskNO_AFFINITY,
                                                       MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (task_ok != pdPASS) {
    set_started_state(false);
    s_task_handle = NULL;
    ESP_LOGE(TAG, "Failed to create snapshot task");
    return ESP_ERR_NO_MEM;
  }

  ESP_LOGI(TAG,
           "Snapshot publisher started (topic=%s interval_ms=%d)",
           s_snapshot_topic,
           CONFIG_THEO_CAMERA_SNAPSHOT_INTERVAL_MS);
  return ESP_OK;
}

esp_err_t camera_snapshot_publisher_stop(void)
{
  taskENTER_CRITICAL(&s_state_lock);
  TaskHandle_t task = s_task_handle;
  if (!s_started || task == NULL) {
    s_started = false;
    s_stop_requested = false;
    taskEXIT_CRITICAL(&s_state_lock);
    return ESP_OK;
  }
  s_stop_requested = true;
  taskEXIT_CRITICAL(&s_state_lock);

  xTaskNotifyGive(task);

  const int64_t deadline_us = esp_timer_get_time() + (2LL * 1000LL * 1000LL);
  while (camera_snapshot_publisher_get_task_handle() != NULL && esp_timer_get_time() < deadline_us) {
    vTaskDelay(pdMS_TO_TICKS(20));
  }

  if (camera_snapshot_publisher_get_task_handle() != NULL) {
    ESP_LOGW(TAG, "Timed out waiting for snapshot task to stop");
    return ESP_ERR_TIMEOUT;
  }

  ESP_LOGI(TAG, "Snapshot publisher stopped");
  return ESP_OK;
}

TaskHandle_t camera_snapshot_publisher_get_task_handle(void)
{
  taskENTER_CRITICAL(&s_state_lock);
  TaskHandle_t task = s_task_handle;
  taskEXIT_CRITICAL(&s_state_lock);
  return task;
}

size_t camera_snapshot_publisher_get_task_stack_size_bytes(void)
{
  return CAMERA_SNAPSHOT_TASK_STACK_BYTES;
}

static void camera_snapshot_task(void *arg)
{
  (void)arg;

  esp_err_t err = acquire_mipi_phy_ldo();
  if (err == ESP_OK) {
    err = init_video_framework();
  }
  if (err == ESP_OK) {
    err = open_camera_device();
  }
  if (err == ESP_OK) {
    err = ensure_jpeg_encoder();
  }
  if (err == ESP_OK) {
    err = ensure_jpeg_buffer();
  }
  if (err == ESP_OK) {
    err = ensure_raw_buffer();
  }
  if (err == ESP_OK) {
    err = start_camera_stream();
  }
  if (err == ESP_OK && thermostat_ir_led_init() == ESP_OK) {
    thermostat_ir_led_set(true);
    s_ir_led_enabled = true;
  }

  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Snapshot publisher unavailable: %s", esp_err_to_name(err));
    release_resources();
    taskENTER_CRITICAL(&s_state_lock);
    s_task_handle = NULL;
    s_started = false;
    s_stop_requested = false;
    taskEXIT_CRITICAL(&s_state_lock);
    vTaskDelete(NULL);
    return;
  }

  const TickType_t interval_ticks = pdMS_TO_TICKS(CONFIG_THEO_CAMERA_SNAPSHOT_INTERVAL_MS);
  uint32_t publish_count = 0;
  uint32_t publish_failures = 0;
  uint64_t total_publish_time_us = 0;
  uint64_t max_publish_time_us = 0;
  uint64_t total_publish_bytes = 0;

  while (!stop_requested()) {
    if (ulTaskNotifyTake(pdTRUE, interval_ticks) > 0 && stop_requested()) {
      break;
    }
    if (stop_requested()) {
      break;
    }

    size_t jpeg_size = 0;
    err = encode_snapshot_frame(&jpeg_size);
    if (err != ESP_OK) {
      ESP_LOGW(TAG, "JPEG encode failed: %s", esp_err_to_name(err));
      continue;
    }

    if (!mqtt_manager_is_ready()) {
      continue;
    }

    esp_mqtt_client_handle_t client = mqtt_manager_get_client();
    if (client == NULL) {
      continue;
    }

    const int64_t publish_start_us = esp_timer_get_time();
    int msg_id = esp_mqtt_client_publish(client,
                                         s_snapshot_topic,
                                         (const char *)s_jpeg_buffer,
                                         (int)jpeg_size,
                                         0,
                                         0);
    uint64_t publish_time_us = (uint64_t)(esp_timer_get_time() - publish_start_us);
    publish_count++;
    total_publish_time_us += publish_time_us;
    total_publish_bytes += jpeg_size;
    if (publish_time_us > max_publish_time_us) {
      max_publish_time_us = publish_time_us;
    }

    if (msg_id < 0) {
      publish_failures++;
      ESP_LOGW(TAG, "Snapshot publish failed (bytes=%zu)", jpeg_size);
    }

    if (publish_count >= CAMERA_SNAPSHOT_METRICS_BATCH_SIZE) {
      log_publish_metrics(publish_count,
                          publish_failures,
                          total_publish_time_us,
                          max_publish_time_us,
                          total_publish_bytes);
      publish_count = 0;
      publish_failures = 0;
      total_publish_time_us = 0;
      max_publish_time_us = 0;
      total_publish_bytes = 0;
    }
  }

  release_resources();

  taskENTER_CRITICAL(&s_state_lock);
  s_task_handle = NULL;
  s_started = false;
  s_stop_requested = false;
  taskEXIT_CRITICAL(&s_state_lock);

  vTaskDelete(NULL);
}

static esp_err_t build_snapshot_topic(void)
{
  const char *device_root = device_identity_get_theo_device_topic_root();
  ESP_RETURN_ON_FALSE(device_root != NULL && device_root[0] != '\0', ESP_ERR_INVALID_STATE, TAG,
                      "Device topic root unavailable");

  int written = snprintf(s_snapshot_topic,
                         sizeof(s_snapshot_topic),
                         "%s%s",
                         device_root,
                         CAMERA_SNAPSHOT_TOPIC_SUFFIX);
  ESP_RETURN_ON_FALSE(written > 0 && written < (int)sizeof(s_snapshot_topic), ESP_ERR_INVALID_SIZE, TAG,
                      "Snapshot topic overflow");
  return ESP_OK;
}

static esp_err_t acquire_mipi_phy_ldo(void)
{
  if (s_ldo_mipi_phy != NULL) {
    return ESP_OK;
  }

  esp_ldo_channel_config_t cfg = {
    .chan_id = 3,
    .voltage_mv = 2500,
  };
  esp_err_t err = esp_ldo_acquire_channel(&cfg, &s_ldo_mipi_phy);
  if (err == ESP_ERR_INVALID_STATE) {
    s_ldo_mipi_phy = NULL;
    return ESP_OK;
  }
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to acquire MIPI LDO: %s", esp_err_to_name(err));
  }
  return err;
}

static void release_mipi_phy_ldo(void)
{
  if (s_ldo_mipi_phy != NULL) {
    esp_ldo_release_channel(s_ldo_mipi_phy);
    s_ldo_mipi_phy = NULL;
  }
}

static esp_err_t init_video_framework(void)
{
  if (s_video_initialized) {
    return ESP_OK;
  }

  i2c_master_bus_handle_t i2c = bsp_i2c_get_handle();
  ESP_RETURN_ON_FALSE(i2c != NULL, ESP_ERR_INVALID_STATE, TAG, "bsp_i2c_get_handle returned NULL");

  esp_video_init_csi_config_t csi_cfg = {
    .sccb_config = {
      .init_sccb = false,
      .i2c_handle = i2c,
      .freq = 100000,
    },
    .reset_pin = -1,
    .pwdn_pin = -1,
    .dont_init_ldo = true,
  };

  esp_video_init_config_t video_cfg = {
    .csi = &csi_cfg,
  };

  esp_err_t err = esp_video_init(&video_cfg);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "esp_video_init failed: %s", esp_err_to_name(err));
    return err;
  }

  s_video_initialized = true;
  return ESP_OK;
}

static void deinit_video_framework(void)
{
  if (!s_video_initialized) {
    return;
  }

  esp_err_t err = esp_video_deinit();
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "esp_video_deinit failed: %s", esp_err_to_name(err));
    return;
  }

  s_video_initialized = false;
}

static esp_err_t open_camera_device(void)
{
  if (s_camera_fd >= 0) {
    return ESP_OK;
  }

  s_camera_fd = open(ESP_VIDEO_MIPI_CSI_DEVICE_NAME, O_RDWR);
  if (s_camera_fd < 0) {
    ESP_LOGE(TAG, "Failed to open %s: %s", ESP_VIDEO_MIPI_CSI_DEVICE_NAME, strerror(errno));
    return ESP_FAIL;
  }

  ESP_RETURN_ON_ERROR(select_camera_format(), TAG, "camera format negotiation failed");
  esp_err_t tuning_err = apply_camera_tuning_controls();
  if (tuning_err != ESP_OK) {
    ESP_LOGW(TAG, "Camera tuning controls incomplete: %s", esp_err_to_name(tuning_err));
  }
  ESP_RETURN_ON_ERROR(allocate_camera_buffers(), TAG, "camera buffer allocation failed");

  return ESP_OK;
}

static void close_camera_device(void)
{
  stop_camera_stream();

  for (size_t i = 0; i < s_camera_buffer_count; ++i) {
    if (s_camera_buffers[i].start != NULL && s_camera_buffers[i].length > 0) {
      munmap(s_camera_buffers[i].start, s_camera_buffers[i].length);
      s_camera_buffers[i].start = NULL;
      s_camera_buffers[i].length = 0;
    }
  }
  s_camera_buffer_count = 0;
  s_camera_row_stride = 0;
  s_frame_size_bytes = 0;
  s_camera_pixfmt = 0;

  if (s_camera_fd >= 0) {
    close(s_camera_fd);
    s_camera_fd = -1;
  }
}

static esp_err_t select_camera_format(void)
{
  const uint32_t preferred_formats[] = {
    V4L2_PIX_FMT_RGB565,
    V4L2_PIX_FMT_RGB24,
  };

  uint32_t selected_format = 0;
  for (size_t preferred = 0; preferred < sizeof(preferred_formats) / sizeof(preferred_formats[0]); ++preferred) {
    for (uint32_t index = 0;; ++index) {
      struct v4l2_fmtdesc fmt_desc = {
        .index = index,
        .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
      };
      if (ioctl(s_camera_fd, VIDIOC_ENUM_FMT, &fmt_desc) != 0) {
        break;
      }
      if (fmt_desc.pixelformat == preferred_formats[preferred]) {
        selected_format = fmt_desc.pixelformat;
        break;
      }
    }
    if (selected_format != 0) {
      break;
    }
  }

  ESP_RETURN_ON_FALSE(selected_format != 0,
                      ESP_ERR_NOT_SUPPORTED,
                      TAG,
                      "No RGB capture format found");

  struct v4l2_format fmt = {
    .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
  };
  fmt.fmt.pix.width = CAMERA_SNAPSHOT_WIDTH;
  fmt.fmt.pix.height = CAMERA_SNAPSHOT_HEIGHT;
  fmt.fmt.pix.pixelformat = selected_format;
  fmt.fmt.pix.field = V4L2_FIELD_NONE;

  if (ioctl(s_camera_fd, VIDIOC_S_FMT, &fmt) != 0) {
    ESP_LOGE(TAG, "VIDIOC_S_FMT failed: %s", strerror(errno));
    return ESP_FAIL;
  }

  ESP_RETURN_ON_FALSE(fmt.fmt.pix.width == CAMERA_SNAPSHOT_WIDTH &&
                          fmt.fmt.pix.height == CAMERA_SNAPSHOT_HEIGHT,
                      ESP_ERR_INVALID_SIZE,
                      TAG,
                      "Camera format mismatch: got %ux%u",
                      fmt.fmt.pix.width,
                      fmt.fmt.pix.height);
  ESP_RETURN_ON_FALSE(fmt.fmt.pix.pixelformat == selected_format,
                      ESP_ERR_NOT_SUPPORTED,
                      TAG,
                      "Camera pixel format mismatch");

  s_camera_pixfmt = fmt.fmt.pix.pixelformat;
  s_camera_row_stride = fmt.fmt.pix.bytesperline;
  s_frame_size_bytes = fmt.fmt.pix.sizeimage;
  if (s_frame_size_bytes == 0) {
    s_frame_size_bytes = packed_frame_bytes(s_camera_pixfmt);
  }

  ESP_LOGI(TAG,
           "Camera ready (%ux%u pixfmt=%s stride=%zu frame_bytes=%zu)",
           CAMERA_SNAPSHOT_WIDTH,
           CAMERA_SNAPSHOT_HEIGHT,
           pixfmt_name(s_camera_pixfmt),
           s_camera_row_stride,
           s_frame_size_bytes);
  return ESP_OK;
}

static esp_err_t apply_camera_tuning_controls(void)
{
  esp_err_t err = set_video_control(s_camera_fd,
                                    V4L2_CID_USER_CLASS,
                                    V4L2_CID_EXPOSURE,
                                    CAMERA_SNAPSHOT_AE_TARGET,
                                    "AE target");

  esp_err_t isp_err = apply_isp_balance_controls();
  if (err != ESP_OK) {
    return err;
  }
  if (isp_err != ESP_OK) {
    return isp_err;
  }

  ESP_LOGI(TAG,
           "Camera tuning applied (ae_target=%d red_balance=%d blue_balance=%d)",
           CAMERA_SNAPSHOT_AE_TARGET,
           CAMERA_SNAPSHOT_RED_BALANCE,
           CAMERA_SNAPSHOT_BLUE_BALANCE);
  return ESP_OK;
}

static esp_err_t apply_isp_balance_controls(void)
{
  int isp_fd = open(ESP_VIDEO_ISP1_DEVICE_NAME, O_RDWR);
  if (isp_fd < 0) {
    ESP_LOGW(TAG, "Failed to open %s for ISP tuning: %s", ESP_VIDEO_ISP1_DEVICE_NAME, strerror(errno));
    return ESP_FAIL;
  }

  esp_err_t err = set_video_control(isp_fd,
                                    V4L2_CID_USER_CLASS,
                                    V4L2_CID_RED_BALANCE,
                                    CAMERA_SNAPSHOT_RED_BALANCE,
                                    "red balance");
  esp_err_t blue_err = set_video_control(isp_fd,
                                         V4L2_CID_USER_CLASS,
                                         V4L2_CID_BLUE_BALANCE,
                                         CAMERA_SNAPSHOT_BLUE_BALANCE,
                                         "blue balance");

  close(isp_fd);

  if (err != ESP_OK) {
    return err;
  }
  return blue_err;
}

static esp_err_t set_video_control(int fd, uint32_t ctrl_class, uint32_t id, int32_t value, const char *name)
{
  struct v4l2_ext_control control[1] = {
    {
      .id = id,
      .value = value,
    },
  };
  struct v4l2_ext_controls controls = {
    .ctrl_class = ctrl_class,
    .count = 1,
    .controls = control,
  };

  if (ioctl(fd, VIDIOC_S_EXT_CTRLS, &controls) != 0) {
    ESP_LOGW(TAG, "Failed to set %s=%ld: %s", name, (long)value, strerror(errno));
    return ESP_FAIL;
  }

  return ESP_OK;
}

static esp_err_t allocate_camera_buffers(void)
{
  struct v4l2_requestbuffers req = {
    .count = CAMERA_SNAPSHOT_BUFFER_COUNT,
    .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
    .memory = V4L2_MEMORY_MMAP,
  };

  if (ioctl(s_camera_fd, VIDIOC_REQBUFS, &req) != 0) {
    ESP_LOGE(TAG, "VIDIOC_REQBUFS failed: %s", strerror(errno));
    return ESP_FAIL;
  }

  ESP_RETURN_ON_FALSE(req.count >= CAMERA_SNAPSHOT_BUFFER_COUNT,
                      ESP_ERR_NO_MEM,
                      TAG,
                      "Only %u camera buffers allocated",
                      req.count);

  s_camera_buffer_count = req.count;
  for (size_t i = 0; i < s_camera_buffer_count; ++i) {
    struct v4l2_buffer buf = {
      .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
      .memory = V4L2_MEMORY_MMAP,
      .index = i,
    };

    if (ioctl(s_camera_fd, VIDIOC_QUERYBUF, &buf) != 0) {
      ESP_LOGE(TAG, "VIDIOC_QUERYBUF[%zu] failed: %s", i, strerror(errno));
      return ESP_FAIL;
    }

    void *mapped = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, s_camera_fd, buf.m.offset);
    if (mapped == MAP_FAILED) {
      ESP_LOGE(TAG, "mmap[%zu] failed: %s", i, strerror(errno));
      return ESP_ERR_NO_MEM;
    }

    s_camera_buffers[i].start = mapped;
    s_camera_buffers[i].length = buf.length;

    if (ioctl(s_camera_fd, VIDIOC_QBUF, &buf) != 0) {
      ESP_LOGE(TAG, "VIDIOC_QBUF[%zu] failed: %s", i, strerror(errno));
      return ESP_FAIL;
    }
  }

  return ESP_OK;
}

static esp_err_t start_camera_stream(void)
{
  if (s_camera_streaming) {
    return ESP_OK;
  }

  enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if (ioctl(s_camera_fd, VIDIOC_STREAMON, &type) != 0) {
    ESP_LOGE(TAG, "VIDIOC_STREAMON failed: %s", strerror(errno));
    return ESP_FAIL;
  }

  s_camera_streaming = true;
  return skip_startup_frames();
}

static void stop_camera_stream(void)
{
  if (s_camera_fd < 0 || !s_camera_streaming) {
    return;
  }

  enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if (ioctl(s_camera_fd, VIDIOC_STREAMOFF, &type) != 0 && errno != EINVAL) {
    ESP_LOGW(TAG, "VIDIOC_STREAMOFF failed: %s", strerror(errno));
  }

  s_camera_streaming = false;
}

static esp_err_t skip_startup_frames(void)
{
  for (int i = 0; i < CAMERA_SNAPSHOT_STARTUP_SKIP_FRAMES; ++i) {
    struct v4l2_buffer buf = {
      .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
      .memory = V4L2_MEMORY_MMAP,
    };
    if (ioctl(s_camera_fd, VIDIOC_DQBUF, &buf) != 0) {
      ESP_LOGE(TAG, "Startup VIDIOC_DQBUF failed: %s", strerror(errno));
      return ESP_FAIL;
    }
    if (ioctl(s_camera_fd, VIDIOC_QBUF, &buf) != 0) {
      ESP_LOGE(TAG, "Startup VIDIOC_QBUF failed: %s", strerror(errno));
      return ESP_FAIL;
    }
  }
  return ESP_OK;
}

static esp_err_t ensure_jpeg_encoder(void)
{
  if (s_jpeg_encoder != NULL) {
    return ESP_OK;
  }

  jpeg_encode_engine_cfg_t cfg = {
    .intr_priority = 0,
    .timeout_ms = CAMERA_SNAPSHOT_CAPTURE_TIMEOUT_MS,
  };
  return jpeg_new_encoder_engine(&cfg, &s_jpeg_encoder);
}

static void release_jpeg_encoder(void)
{
  if (s_jpeg_encoder != NULL) {
    esp_err_t err = jpeg_del_encoder_engine(s_jpeg_encoder);
    if (err != ESP_OK) {
      ESP_LOGW(TAG, "jpeg_del_encoder_engine failed: %s", esp_err_to_name(err));
    }
    s_jpeg_encoder = NULL;
  }
}

static esp_err_t ensure_jpeg_buffer(void)
{
  if (s_jpeg_buffer != NULL) {
    return ESP_OK;
  }

  jpeg_encode_memory_alloc_cfg_t mem_cfg = {
    .buffer_direction = JPEG_ENC_ALLOC_OUTPUT_BUFFER,
  };

  s_jpeg_buffer = jpeg_alloc_encoder_mem(CAMERA_SNAPSHOT_JPEG_BUFFER_BYTES,
                                         &mem_cfg,
                                         &s_jpeg_buffer_size);
  if (s_jpeg_buffer == NULL) {
    s_jpeg_buffer_size = 0;
    return ESP_ERR_NO_MEM;
  }

  return ESP_OK;
}

static void release_jpeg_buffer(void)
{
  if (s_jpeg_buffer != NULL) {
    free(s_jpeg_buffer);
    s_jpeg_buffer = NULL;
  }
  s_jpeg_buffer_size = 0;
}

static esp_err_t ensure_raw_buffer(void)
{
  if (s_raw_buffer != NULL) {
    return ESP_OK;
  }

  s_raw_buffer_size = packed_frame_bytes(s_camera_pixfmt);
  ESP_RETURN_ON_FALSE(s_raw_buffer_size > 0,
                      ESP_ERR_NOT_SUPPORTED,
                      TAG,
                      "Unsupported raw buffer format: %s",
                      pixfmt_name(s_camera_pixfmt));

  s_raw_buffer = heap_caps_malloc(s_raw_buffer_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (s_raw_buffer == NULL) {
    s_raw_buffer_size = 0;
    return ESP_ERR_NO_MEM;
  }

  return ESP_OK;
}

static void release_raw_buffer(void)
{
  if (s_raw_buffer != NULL) {
    heap_caps_free(s_raw_buffer);
    s_raw_buffer = NULL;
  }
  s_raw_buffer_size = 0;
}

static size_t packed_frame_bytes(uint32_t pixfmt)
{
  if (pixfmt == V4L2_PIX_FMT_RGB565) {
    return (size_t)CAMERA_SNAPSHOT_WIDTH * (size_t)CAMERA_SNAPSHOT_HEIGHT * 2U;
  }
  if (pixfmt == V4L2_PIX_FMT_RGB24) {
    return (size_t)CAMERA_SNAPSHOT_WIDTH * (size_t)CAMERA_SNAPSHOT_HEIGHT * 3U;
  }
  return 0;
}

static jpeg_enc_input_format_t jpeg_src_type_for_pixfmt(uint32_t pixfmt)
{
  if (pixfmt == V4L2_PIX_FMT_RGB565) {
    return JPEG_ENCODE_IN_FORMAT_RGB565;
  }
  return JPEG_ENCODE_IN_FORMAT_RGB888;
}

static jpeg_down_sampling_type_t jpeg_subsample_for_pixfmt(uint32_t pixfmt)
{
  if (pixfmt == V4L2_PIX_FMT_RGB24) {
    return JPEG_DOWN_SAMPLING_YUV444;
  }
  return JPEG_DOWN_SAMPLING_YUV422;
}

static void copy_frame_to_jpeg_input(const uint8_t *frame,
                                     size_t source_stride,
                                     size_t bytes_per_pixel)
{
  const size_t packed_stride = (size_t)CAMERA_SNAPSHOT_WIDTH * bytes_per_pixel;

  for (size_t y = 0; y < CAMERA_SNAPSHOT_HEIGHT; ++y) {
    const uint8_t *src_row = frame + ((CAMERA_SNAPSHOT_HEIGHT - 1U - y) * source_stride);
    uint8_t *dst_row = s_raw_buffer + (y * packed_stride);

    for (size_t x = 0; x < CAMERA_SNAPSHOT_WIDTH; ++x) {
      memcpy(dst_row + (x * bytes_per_pixel),
             src_row + ((CAMERA_SNAPSHOT_WIDTH - 1U - x) * bytes_per_pixel),
             bytes_per_pixel);
    }
  }
}

static esp_err_t encode_snapshot_frame(size_t *jpeg_size)
{
  ESP_RETURN_ON_FALSE(jpeg_size != NULL,
                      ESP_ERR_INVALID_ARG,
                      TAG,
                      "JPEG size pointer missing");
  ESP_RETURN_ON_FALSE(s_jpeg_encoder != NULL && s_jpeg_buffer != NULL,
                      ESP_ERR_INVALID_STATE,
                      TAG,
                      "JPEG encoder unavailable");
  ESP_RETURN_ON_FALSE(s_raw_buffer != NULL && s_raw_buffer_size > 0,
                      ESP_ERR_INVALID_STATE,
                      TAG,
                      "Raw buffer unavailable");

  struct v4l2_buffer buf = {
    .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
    .memory = V4L2_MEMORY_MMAP,
  };
  if (ioctl(s_camera_fd, VIDIOC_DQBUF, &buf) != 0) {
    ESP_LOGE(TAG, "VIDIOC_DQBUF failed: %s", strerror(errno));
    return ESP_FAIL;
  }

  ESP_RETURN_ON_FALSE(buf.index < s_camera_buffer_count,
                      ESP_ERR_INVALID_RESPONSE,
                      TAG,
                      "Camera buffer index overflow: %u",
                      buf.index);

  const size_t bytes_per_pixel = (s_camera_pixfmt == V4L2_PIX_FMT_RGB565) ? 2U : 3U;
  const size_t packed_stride = (size_t)CAMERA_SNAPSHOT_WIDTH * bytes_per_pixel;
  const size_t source_stride = s_camera_row_stride >= packed_stride ? s_camera_row_stride : packed_stride;
  const size_t required_bytes = source_stride * (size_t)CAMERA_SNAPSHOT_HEIGHT;

  if (buf.bytesused < required_bytes) {
    ESP_LOGE(TAG,
             "Frame too small for %s: bytesused=%u required=%zu stride=%zu",
             pixfmt_name(s_camera_pixfmt),
             buf.bytesused,
             required_bytes,
             source_stride);
    (void)ioctl(s_camera_fd, VIDIOC_QBUF, &buf);
    return ESP_ERR_INVALID_SIZE;
  }

  const uint8_t *frame = (const uint8_t *)s_camera_buffers[buf.index].start;
  copy_frame_to_jpeg_input(frame, source_stride, bytes_per_pixel);

  if (ioctl(s_camera_fd, VIDIOC_QBUF, &buf) != 0) {
    ESP_LOGE(TAG, "VIDIOC_QBUF failed: %s", strerror(errno));
    return ESP_FAIL;
  }

  jpeg_encode_cfg_t cfg = {
    .height = CAMERA_SNAPSHOT_HEIGHT,
    .width = CAMERA_SNAPSHOT_WIDTH,
    .src_type = jpeg_src_type_for_pixfmt(s_camera_pixfmt),
    .sub_sample = jpeg_subsample_for_pixfmt(s_camera_pixfmt),
    .image_quality = CAMERA_SNAPSHOT_JPEG_QUALITY,
  };

  uint32_t encoded_size = 0;
  esp_err_t err = jpeg_encoder_process(s_jpeg_encoder,
                                       &cfg,
                                       s_raw_buffer,
                                       (uint32_t)s_raw_buffer_size,
                                       s_jpeg_buffer,
                                       (uint32_t)s_jpeg_buffer_size,
                                       &encoded_size);
  if (err != ESP_OK) {
    return err;
  }

  *jpeg_size = encoded_size;
  return ESP_OK;
}

static void release_resources(void)
{
  if (s_ir_led_enabled) {
    thermostat_ir_led_set(false);
    s_ir_led_enabled = false;
  }
  stop_camera_stream();
  release_raw_buffer();
  release_jpeg_buffer();
  release_jpeg_encoder();
  close_camera_device();
  deinit_video_framework();
  release_mipi_phy_ldo();
}

static bool stop_requested(void)
{
  taskENTER_CRITICAL(&s_state_lock);
  bool requested = s_stop_requested;
  taskEXIT_CRITICAL(&s_state_lock);
  return requested;
}

static void set_started_state(bool started)
{
  taskENTER_CRITICAL(&s_state_lock);
  s_started = started;
  s_stop_requested = false;
  taskEXIT_CRITICAL(&s_state_lock);
}

static void log_publish_metrics(uint32_t publish_count,
                                uint32_t publish_failures,
                                uint64_t total_publish_time_us,
                                uint64_t max_publish_time_us,
                                uint64_t total_publish_bytes)
{
  if (publish_count == 0) {
    return;
  }

  uint32_t avg_publish_us = (uint32_t)(total_publish_time_us / publish_count);
  uint32_t max_publish_us = (uint32_t)max_publish_time_us;
  uint32_t avg_publish_bytes = (uint32_t)(total_publish_bytes / publish_count);

  ESP_LOGI(TAG,
           "snapshot_publish_metrics count=%u failures=%u avg_publish_us=%u max_publish_us=%u avg_bytes=%u interval_ms=%d",
           publish_count,
           publish_failures,
           avg_publish_us,
           max_publish_us,
           avg_publish_bytes,
           CONFIG_THEO_CAMERA_SNAPSHOT_INTERVAL_MS);
}

static const char *pixfmt_name(uint32_t pixfmt)
{
  static char name[5];
  name[0] = pixfmt & 0xFF;
  name[1] = (pixfmt >> 8) & 0xFF;
  name[2] = (pixfmt >> 16) & 0xFF;
  name[3] = (pixfmt >> 24) & 0xFF;
  name[4] = '\0';
  return name;
}

#else

esp_err_t camera_snapshot_publisher_start(void)
{
  return ESP_OK;
}

esp_err_t camera_snapshot_publisher_stop(void)
{
  return ESP_OK;
}

TaskHandle_t camera_snapshot_publisher_get_task_handle(void)
{
  return NULL;
}

size_t camera_snapshot_publisher_get_task_stack_size_bytes(void)
{
  return 0;
}

#endif
