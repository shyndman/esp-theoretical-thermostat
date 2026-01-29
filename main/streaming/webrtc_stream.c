#include "webrtc_stream.h"

#if CONFIG_THEO_CAMERA_ENABLE && CONFIG_THEO_WEBRTC_ENABLE

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "bsp/esp32_p4_nano.h"
#include "connectivity/wifi_remote_manager.h"
#include "driver/i2c_master.h"
#include "esp_capture.h"
#include "esp_capture_defaults.h"
#include "esp_capture_sink.h"
#include "esp_capture_advance.h"
#include "esp_check.h"
#include "esp_video_enc_default.h"
#include "esp_video_enc.h"
#include "esp_event.h"
#include "esp_ldo_regulator.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_peer_default.h"
#include "esp_timer.h"
#include "esp_video_init.h"
#include "esp_webrtc.h"
#include "esp_webrtc_defaults.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "linux/videodev2.h"
#include "media_lib_adapter.h"
#include "media_lib_os.h"
#include "sdkconfig.h"
#include "thermostat/ir_led.h"

#define TAG "webrtc_stream"

static void log_internal_heap_state(const char *label, esp_log_level_t level, bool include_minimum)
{
  size_t free_bytes = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  size_t largest_block = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
  if (include_minimum) {
    size_t min_free = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
    ESP_LOG_LEVEL(level, TAG, "%s internal heap: free=%zu largest_block=%zu min_free=%zu",
                  label,
                  free_bytes,
                  largest_block,
                  min_free);
  } else {
    ESP_LOG_LEVEL(level, TAG, "%s internal heap: free=%zu largest_block=%zu",
                  label,
                  free_bytes,
                  largest_block);
  }
}

#define WEBRTC_FRAME_WIDTH   1280
#define WEBRTC_FRAME_HEIGHT  960
#define WEBRTC_FRAME_FPS     9
#define WEBRTC_RETRY_DELAY_US (5 * 1000 * 1000)
#define WEBRTC_QUERY_INTERVAL_US (30 * 1000 * 1000)

#define WEBRTC_TASK_EVENT_START   BIT0
#define WEBRTC_TASK_EVENT_STOP    BIT1
#define WEBRTC_TASK_EVENT_RESTART BIT2

static esp_capture_handle_t s_capture_handle;
static esp_capture_video_src_if_t *s_video_src;
static esp_capture_sink_handle_t s_video_sink;
static esp_webrtc_handle_t s_webrtc;

static esp_ldo_channel_handle_t s_ldo_mipi_phy;
static bool s_video_initialized;

static SemaphoreHandle_t s_state_mutex;
static TaskHandle_t s_worker_task;
static esp_event_handler_instance_t s_wifi_handler;
static esp_event_handler_instance_t s_ip_handler;

static bool s_module_running;
static bool s_wifi_ready;
static bool s_media_lib_ready;
static bool s_capture_ready;
static bool s_handlers_registered;
static bool s_ir_led_ready;
static int64_t s_next_retry_time_us;
static int64_t s_next_query_time_us;

static char s_signal_url[192];

static void request_task_event(uint32_t bits);
static int webrtc_event_handler(esp_webrtc_event_t *event, void *ctx);
static void shutdown_camera(void);

static void thread_scheduler(const char *thread_name, media_lib_thread_cfg_t *schedule_cfg)
{
  if (thread_name == NULL || schedule_cfg == NULL) {
    return;
  }
  if (strcmp(thread_name, "venc_0") == 0) {
    schedule_cfg->priority = 10;
  } else if (strcmp(thread_name, "pc_task") == 0) {
    schedule_cfg->stack_size = 25 * 1024;
    schedule_cfg->priority = 18;
    schedule_cfg->core_id = 1;
  } else if (strcmp(thread_name, "AUD_SRC") == 0) {
    schedule_cfg->priority = 15;
  } else if (strcmp(thread_name, "aenc_0") == 0) {
    schedule_cfg->stack_size = 40 * 1024;
    schedule_cfg->priority = 10;
    schedule_cfg->core_id = 1;
  }
  if (strcmp(thread_name, "start") == 0) {
    schedule_cfg->stack_size = 6 * 1024;
  }
}

static void capture_scheduler(const char *name, esp_capture_thread_schedule_cfg_t *schedule_cfg)
{
  if (!schedule_cfg) {
    return;
  }
  media_lib_thread_cfg_t cfg = {
    .stack_size = schedule_cfg->stack_size,
    .priority = schedule_cfg->priority,
    .core_id = schedule_cfg->core_id,
  };
  thread_scheduler(name, &cfg);
  schedule_cfg->stack_in_ext = true;
  schedule_cfg->stack_size = cfg.stack_size;
  schedule_cfg->priority = cfg.priority;
  schedule_cfg->core_id = cfg.core_id;
}

static esp_err_t acquire_mipi_phy_ldo(void)
{
  if (s_ldo_mipi_phy) {
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
  if (s_ldo_mipi_phy) {
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
  if (i2c == NULL) {
    ESP_LOGE(TAG, "bsp_i2c_get_handle returned NULL");
    return ESP_ERR_INVALID_STATE;
  }

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

static void configure_camera_flip(void)
{
  int fd = open("/dev/video0", O_RDWR);
  if (fd < 0) {
    ESP_LOGW(TAG, "Failed to open /dev/video0 for flip config: %s", strerror(errno));
    return;

  }
  struct v4l2_ext_controls ctrls = {0};
  struct v4l2_ext_control ctrl[2] = {{0}};
  ctrls.ctrl_class = V4L2_CTRL_CLASS_USER;
  ctrls.count = 2;
  ctrls.controls = ctrl;
  ctrl[0].id = V4L2_CID_HFLIP;
  ctrl[0].value = 1;
  ctrl[1].id = V4L2_CID_VFLIP;
  ctrl[1].value = 1;
  if (ioctl(fd, VIDIOC_S_EXT_CTRLS, &ctrls) != 0) {
    ESP_LOGW(TAG, "Failed to set camera flip controls: %s", strerror(errno));
  }
  close(fd);
}

static esp_err_t ensure_camera_ready(void)
{
  if (s_capture_ready) {
    return ESP_OK;
  }

  ESP_RETURN_ON_ERROR(acquire_mipi_phy_ldo(), TAG, "acquire LDO failed");
  ESP_RETURN_ON_ERROR(init_video_framework(), TAG, "video init failed");

  // Register default video encoders (H.264 hardware encoder)
  esp_err_t err = esp_video_enc_register_default();
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "esp_video_enc_register_default failed: %s", esp_err_to_name(err));
  }

  esp_capture_video_v4l2_src_cfg_t cfg = {
    .dev_name = "/dev/video0",
    .buf_count = 3,
  };

  s_video_src = esp_capture_new_video_v4l2_src(&cfg);
  if (s_video_src == NULL) {
    ESP_LOGE(TAG, "Failed to create V4L2 capture source");
    return ESP_ERR_NO_MEM;
  }

  esp_capture_cfg_t capture_cfg = {
    .sync_mode = ESP_CAPTURE_SYNC_MODE_NONE,
    .video_src = s_video_src,
  };

  err = esp_capture_open(&capture_cfg, &s_capture_handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "esp_capture_open failed: %s", esp_err_to_name(err));
    return err;
  }

  log_internal_heap_state("Before esp_capture_sink_setup", ESP_LOG_INFO, false);

  // Configure H.264 sink for WebRTC
  esp_capture_sink_cfg_t sink_cfg = {
    .video_info = {
      .format_id = ESP_CAPTURE_FMT_ID_H264,
      .width = WEBRTC_FRAME_WIDTH,
      .height = WEBRTC_FRAME_HEIGHT,
      .fps = WEBRTC_FRAME_FPS,
    },
    .audio_info = {
      .format_id = ESP_CAPTURE_FMT_ID_NONE,
      .sample_rate = 0,
      .channel = 0,
      .bits_per_sample = 0,
    },
  };

  err = esp_capture_sink_setup(s_capture_handle, 0, &sink_cfg, &s_video_sink);
  if (err != ESP_OK) {
    log_internal_heap_state("esp_capture_sink_setup failed", ESP_LOG_WARN, true);
    ESP_LOGE(TAG, "esp_capture_sink_setup failed: %s", esp_err_to_name(err));
    esp_capture_close(s_capture_handle);
    s_capture_handle = NULL;
    return err;
  }

  // Build pipeline with encoder element
  const char *vid_elements[] = {"vid_fps_cvt", "vid_enc"};
  err = esp_capture_sink_build_pipeline(s_video_sink, ESP_CAPTURE_STREAM_TYPE_VIDEO,
                                        vid_elements, sizeof(vid_elements) / sizeof(vid_elements[0]));
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "esp_capture_sink_build_pipeline failed: %s", esp_err_to_name(err));
    esp_capture_close(s_capture_handle);
    s_capture_handle = NULL;
    return err;
  }

  // Enable the sink to start capturing
  err = esp_capture_sink_enable(s_video_sink, ESP_CAPTURE_RUN_MODE_ALWAYS);
  if (err != ESP_OK) {
    log_internal_heap_state("esp_capture_sink_enable failed", ESP_LOG_WARN, true);
    ESP_LOGE(TAG, "esp_capture_sink_enable failed: %s", esp_err_to_name(err));
    esp_capture_close(s_capture_handle);
    s_capture_handle = NULL;
    return err;
  }

  log_internal_heap_state("After esp_capture_sink_enable", ESP_LOG_INFO, false);

  configure_camera_flip();
  s_capture_ready = true;
  return ESP_OK;
}

static void shutdown_camera(void)
{
  if (!s_capture_ready) {
    return;
  }
  if (s_capture_handle) {
    esp_capture_stop(s_capture_handle);
    esp_capture_close(s_capture_handle);
    s_capture_handle = NULL;
  }
  s_video_sink = NULL;
  s_capture_ready = false;
  s_video_src = NULL;
}

static void ensure_ir_led_ready(void)
{
  if (!s_ir_led_ready) {
    if (thermostat_ir_led_init() == ESP_OK) {
      s_ir_led_ready = true;
    }
  }
}

static void ensure_media_lib_ready(void)
{
  if (s_media_lib_ready) {
    return;
  }
  if (media_lib_add_default_adapter() != ESP_OK) {
    ESP_LOGW(TAG, "media_lib_add_default_adapter failed");
  }

  esp_capture_set_thread_scheduler(capture_scheduler);
  media_lib_thread_set_schedule_cb(thread_scheduler);
  s_media_lib_ready = true;
}

static void teardown_webrtc(void)
{
  if (s_webrtc) {
    ESP_LOGI(TAG, "Stopping WebRTC publisher");
    esp_webrtc_close(s_webrtc);
    s_webrtc = NULL;
  }
  if (s_ir_led_ready) {
    thermostat_ir_led_set(false);
  }
}

static void build_signal_url(void)
{
  const char *host = CONFIG_THEO_WEBRTC_HOST;
  const char *path = CONFIG_THEO_WEBRTC_PATH[0] ? CONFIG_THEO_WEBRTC_PATH : "/api/webrtc";
  char normalized_path[96] = {0};
  if (path[0] == '/') {
    strlcpy(normalized_path, path, sizeof(normalized_path));
  } else {
    normalized_path[0] = '/';
    strlcpy(normalized_path + 1, path, sizeof(normalized_path) - 1);
  }
  snprintf(s_signal_url, sizeof(s_signal_url), "http://%s:%d%s?dst=%s",
           host,
           CONFIG_THEO_WEBRTC_PORT,
           normalized_path,
           CONFIG_THEO_WEBRTC_STREAM_ID);
}

static esp_err_t start_webrtc_session(void)
{
  if (s_webrtc != NULL) {
    return ESP_OK;
  }

  bool ready = s_module_running && s_wifi_ready;
  if (s_state_mutex) {
    xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    ready = s_module_running && s_wifi_ready;
    xSemaphoreGive(s_state_mutex);
  }
  if (!ready) {
    return ESP_ERR_INVALID_STATE;
  }

  int64_t now = esp_timer_get_time();
  if (now < s_next_retry_time_us) {
    return ESP_ERR_INVALID_STATE;
  }

  ensure_media_lib_ready();
  ESP_RETURN_ON_ERROR(ensure_camera_ready(), TAG, "camera init failed");

  build_signal_url();

  ESP_LOGI(TAG, "Configuring WebRTC: video=%dx%d@%d dir=%d audio_codec=%d",
           WEBRTC_FRAME_WIDTH,
           WEBRTC_FRAME_HEIGHT,
           WEBRTC_FRAME_FPS,
           ESP_PEER_MEDIA_DIR_SEND_ONLY,
           ESP_PEER_AUDIO_CODEC_G711A);

  esp_webrtc_cfg_t cfg = {
    .peer_cfg = {
      .audio_info = {
        .codec = ESP_PEER_AUDIO_CODEC_G711A,
        .sample_rate = 8000,
        .channel = 1,
      },
      .video_info = {
        .codec = ESP_PEER_VIDEO_CODEC_H264,
        .width = WEBRTC_FRAME_WIDTH,
        .height = WEBRTC_FRAME_HEIGHT,
        .fps = WEBRTC_FRAME_FPS,
      },
      .audio_dir = ESP_PEER_MEDIA_DIR_SEND_ONLY,
      .video_dir = ESP_PEER_MEDIA_DIR_SEND_ONLY,
      .ice_trans_policy = ESP_PEER_ICE_TRANS_POLICY_ALL,
      .enable_data_channel = false,
      .no_auto_reconnect = true,
    },
    .signaling_cfg = {
      .signal_url = s_signal_url,
    },
    .peer_impl = esp_peer_get_default_impl(),
    .signaling_impl = esp_signaling_get_whip_impl(),
  };

  int ret = esp_webrtc_open(&cfg, &s_webrtc);
  if (ret != 0) {
    ESP_LOGE(TAG, "esp_webrtc_open failed (%d)", ret);
    s_webrtc = NULL;
    s_next_retry_time_us = now + WEBRTC_RETRY_DELAY_US;
    return ESP_FAIL;
  }

  esp_webrtc_media_provider_t provider = {
    .capture = s_capture_handle,
  };

  esp_webrtc_set_media_provider(s_webrtc, &provider);
  esp_webrtc_set_event_handler(s_webrtc, webrtc_event_handler, NULL);
  esp_webrtc_enable_peer_connection(s_webrtc, true);
  log_internal_heap_state("Before esp_webrtc_start", ESP_LOG_INFO, false);
  ret = esp_webrtc_start(s_webrtc);
  if (ret != 0) {
    log_internal_heap_state("esp_webrtc_start failed", ESP_LOG_WARN, true);
    ESP_LOGE(TAG, "esp_webrtc_start failed (%d)", ret);
    teardown_webrtc();
    s_next_retry_time_us = esp_timer_get_time() + WEBRTC_RETRY_DELAY_US;
    return ESP_FAIL;
  }

  log_internal_heap_state("After esp_webrtc_start", ESP_LOG_INFO, false);

  ESP_LOGI(TAG, "WebRTC publisher started: %s", s_signal_url);
  return ESP_OK;
}

static void webrtc_task(void *arg)
{
  while (true) {
    uint32_t events = 0;
    xTaskNotifyWait(0, UINT32_MAX, &events, pdMS_TO_TICKS(1000));

    if (events & WEBRTC_TASK_EVENT_STOP) {
      teardown_webrtc();
    }

    if ((events & WEBRTC_TASK_EVENT_RESTART) != 0) {
      teardown_webrtc();
    }

    start_webrtc_session();

    if (s_webrtc) {
      int64_t now = esp_timer_get_time();
      if (now >= s_next_query_time_us) {
        esp_webrtc_query(s_webrtc);
        s_next_query_time_us = now + WEBRTC_QUERY_INTERVAL_US;
      }
    }

    bool should_exit = false;
    if (s_state_mutex) {
      xSemaphoreTake(s_state_mutex, portMAX_DELAY);
      should_exit = !s_module_running;
      xSemaphoreGive(s_state_mutex);
    }
    if (should_exit && s_webrtc == NULL) {
      break;
    }
  }

  s_worker_task = NULL;
  vTaskDelete(NULL);
}

static void request_task_event(uint32_t bits)
{
  if (s_worker_task) {
    xTaskNotify(s_worker_task, bits, eSetBits);
  }
}

static int webrtc_event_handler(esp_webrtc_event_t *event, void *ctx)
{
  if (!event) {
    return 0;
  }
  if (!s_module_running) {
    return 0;
  }
  switch (event->type) {
    case ESP_WEBRTC_EVENT_CONNECTED:
      if (s_ir_led_ready) {
        thermostat_ir_led_set(true);
      }
      s_next_retry_time_us = 0;
      break;
    case ESP_WEBRTC_EVENT_CONNECT_FAILED:
    case ESP_WEBRTC_EVENT_DISCONNECTED:
      if (s_ir_led_ready) {
        thermostat_ir_led_set(false);
      }
      s_next_retry_time_us = esp_timer_get_time() + WEBRTC_RETRY_DELAY_US;
      request_task_event(WEBRTC_TASK_EVENT_RESTART);
      break;
    default:
      break;
  }
  return 0;
}

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
  if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
    if (s_state_mutex) {
      xSemaphoreTake(s_state_mutex, portMAX_DELAY);
      s_wifi_ready = false;
      xSemaphoreGive(s_state_mutex);
    }
    request_task_event(WEBRTC_TASK_EVENT_STOP);
  } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
    if (s_state_mutex) {
      xSemaphoreTake(s_state_mutex, portMAX_DELAY);
      s_wifi_ready = true;
      xSemaphoreGive(s_state_mutex);
    }
    request_task_event(WEBRTC_TASK_EVENT_START);
  }
}

esp_err_t webrtc_stream_start(void)
{
  if (!s_state_mutex) {
    s_state_mutex = xSemaphoreCreateMutex();
    if (!s_state_mutex) {
      return ESP_ERR_NO_MEM;
    }
  }

  ensure_ir_led_ready();

  xSemaphoreTake(s_state_mutex, portMAX_DELAY);
  if (s_module_running) {
    xSemaphoreGive(s_state_mutex);
    return ESP_OK;
  }
  s_module_running = true;
  s_next_retry_time_us = 0;
  s_wifi_ready = wifi_remote_manager_is_ready();
  xSemaphoreGive(s_state_mutex);

  if (!s_worker_task) {
    BaseType_t ok = xTaskCreate(webrtc_task, "webrtc", 4096, NULL, 5, &s_worker_task);
    if (ok != pdPASS) {
      ESP_LOGE(TAG, "Failed to create WebRTC service task");
      xSemaphoreTake(s_state_mutex, portMAX_DELAY);
      s_module_running = false;
      xSemaphoreGive(s_state_mutex);
      return ESP_ERR_NO_MEM;
    }
  }

  if (!s_handlers_registered) {
    esp_event_handler_instance_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, wifi_event_handler, NULL, &s_wifi_handler);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL, &s_ip_handler);
    s_handlers_registered = true;
  }

  if (s_wifi_ready) {
    request_task_event(WEBRTC_TASK_EVENT_START);
  }

  return ESP_OK;
}

void webrtc_stream_stop(void)
{
  if (!s_state_mutex) {
    return;
  }

  xSemaphoreTake(s_state_mutex, portMAX_DELAY);
  if (!s_module_running) {
    xSemaphoreGive(s_state_mutex);
    return;
  }
  s_module_running = false;
  xSemaphoreGive(s_state_mutex);

  request_task_event(WEBRTC_TASK_EVENT_STOP);

  if (s_handlers_registered) {
    esp_event_handler_instance_unregister(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, s_wifi_handler);
    esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, s_ip_handler);
    s_handlers_registered = false;
  }

  while (s_worker_task) {
    vTaskDelay(pdMS_TO_TICKS(50));
  }

  teardown_webrtc();
  shutdown_camera();
  release_mipi_phy_ldo();
}

#else

esp_err_t webrtc_stream_start(void)
{
  return ESP_ERR_NOT_SUPPORTED;
}

void webrtc_stream_stop(void) {}

#endif  // CONFIG_THEO_CAMERA_ENABLE && CONFIG_THEO_WEBRTC_ENABLE
