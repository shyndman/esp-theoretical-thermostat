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
#if CONFIG_THEO_MICROPHONE_ENABLE
#include "esp_audio_enc_default.h"
#include "esp_capture_audio_dev_src.h"
#endif
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
#if CONFIG_THEO_MICROPHONE_ENABLE
#include "streaming/microphone_capture.h"
#endif
#include "streaming/whep_endpoint.h"
#include "streaming/whep_signaling.h"

#if CONFIG_THEO_MICROPHONE_ENABLE
esp_gmf_err_t capture_audio_src_el_set_in_frame_samples(esp_gmf_element_handle_t self, int sample_size);
#define MICROPHONE_SAMPLE_RATE_HZ 16000
#define MICROPHONE_FRAME_SAMPLES 320
#define MICROPHONE_CHANNELS 1
#endif

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
#define CAMERA_FPS_LOG_INTERVAL_US (60 * 1000 * 1000)

#define WEBRTC_TASK_EVENT_START   BIT0
#define WEBRTC_TASK_EVENT_STOP    BIT1
#define WEBRTC_TASK_EVENT_RESTART BIT2

#define WHEP_HTTP_TIMEOUT_MS (15000)

typedef struct
{
  char *offer;
  size_t offer_len;
  char *answer;
  size_t answer_len;
  SemaphoreHandle_t done;
  esp_err_t status;
  esp_peer_signaling_whep_cfg_t signal_cfg;
  char stream_id[64];
} whep_request_t;

static esp_capture_handle_t s_capture_handle;
static esp_capture_video_src_if_t *s_video_src;
static esp_capture_sink_handle_t s_video_sink;
static esp_webrtc_handle_t s_webrtc;
#if CONFIG_THEO_MICROPHONE_ENABLE
static esp_capture_audio_src_if_t *s_audio_src;
static bool s_audio_available;
static bool s_audio_failed;
#endif

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
static bool s_peer_logs_tamed;
static int64_t s_next_retry_time_us;
static int64_t s_next_query_time_us;

static int64_t s_next_fps_log_time_us;
static QueueHandle_t s_whep_request_queue;
static bool s_whep_session_gate;
static bool s_whep_endpoint_registered;

static void request_task_event(uint32_t bits);
static int webrtc_event_handler(esp_webrtc_event_t *event, void *ctx);
static void shutdown_camera(void);
static void log_camera_stream_rate(void);
static esp_err_t start_webrtc_session_from_request(whep_request_t *req);
static esp_err_t webrtc_handle_whep_request(const char *stream_id,
                                            const char *offer,
                                            size_t offer_len,
                                            char **answer_out,
                                            size_t *answer_len_out,
                                            void *ctx);
static whep_request_t *whep_request_create(const char *stream_id, const char *offer, size_t offer_len);
static void whep_request_destroy(whep_request_t *req);
static void whep_answer_ready_cb(const uint8_t *data, size_t len, void *ctx);
static void whep_release_gate_if_idle(void);

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

/**
 * Camera control callback for WebRTC streaming.
 * Configures HFLIP, VFLIP, and frame rate before streaming starts.
 *
 * This callback is invoked by esp_capture after opening /dev/video0
 * but before buffer negotiation, ensuring controls are applied to
 * the same FD that will be used for streaming.
 */
static esp_err_t webrtc_camera_ctrl_cb(int fd, void *ctx)
{
  (void)ctx;  // Unused for now, but available for future extension

  ESP_LOGI(TAG, "Configuring camera controls on fd=%d", fd);

  // --- Step 1: Configure HFLIP and VFLIP ---
  struct v4l2_ext_controls ctrls = {0};
  struct v4l2_ext_control ctrl[2] = {{0}};

  ctrls.ctrl_class = V4L2_CTRL_CLASS_USER;
  ctrls.count = 2;
  ctrls.controls = ctrl;

  ctrl[0].id = V4L2_CID_HFLIP;
  ctrl[0].value = 1;  // Enable horizontal flip
  ctrl[1].id = V4L2_CID_VFLIP;
  ctrl[1].value = 1;  // Enable vertical flip

  ESP_LOGI(TAG, "Setting HFLIP=1, VFLIP=1");

  if (ioctl(fd, VIDIOC_S_EXT_CTRLS, &ctrls) != 0) {
    int err = errno;
    ESP_LOGW(TAG, "Failed to set camera flip controls: %s (errno=%d)",
             strerror(err), err);
    // Non-fatal: log warning but continue
  } else {
    ESP_LOGI(TAG, "Camera flip configured: HFLIP=1, VFLIP=1");
  }

  // --- Step 2: Configure Frame Rate ---
  struct v4l2_streamparm parm = {
    .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
  };

  // First, query current settings
  if (ioctl(fd, VIDIOC_G_PARM, &parm) != 0) {
    int err = errno;
    ESP_LOGW(TAG, "VIDIOC_G_PARM failed: %s (errno=%d)", strerror(err), err);
    // Non-fatal: continue without frame rate config
  } else {
    // Check if frame rate control is supported
    if ((parm.parm.capture.capability & V4L2_CAP_TIMEPERFRAME) == 0) {
      ESP_LOGW(TAG, "Camera driver does not support frame rate control");
    } else {
      // Set desired frame rate (matching WEBRTC_FRAME_FPS)
      parm.parm.capture.timeperframe.numerator = 1;
      parm.parm.capture.timeperframe.denominator = WEBRTC_FRAME_FPS;

      ESP_LOGI(TAG, "Setting frame rate to %d fps", WEBRTC_FRAME_FPS);

      if (ioctl(fd, VIDIOC_S_PARM, &parm) != 0) {
        int err = errno;
        ESP_LOGW(TAG, "VIDIOC_S_PARM failed: %s (errno=%d)",
                 strerror(err), err);
      } else {
        // Verify what was actually set
        if (ioctl(fd, VIDIOC_G_PARM, &parm) == 0) {
          float actual_fps = (float)parm.parm.capture.timeperframe.denominator /
                             (float)parm.parm.capture.timeperframe.numerator;
          ESP_LOGI(TAG, "Camera frame rate configured: %.2f fps", actual_fps);
        }
      }
    }
  }

  ESP_LOGI(TAG, "Camera control callback completed");
  return ESP_OK;
}

#if CONFIG_THEO_MICROPHONE_ENABLE
static esp_err_t ensure_microphone_ready(void)
{
  if (s_audio_available) {
    return ESP_OK;
  }
  if (s_audio_failed) {
    return ESP_FAIL;
  }

  esp_err_t err = microphone_capture_init();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Microphone init failed: %s", esp_err_to_name(err));
    s_audio_failed = true;
    return err;
  }

  esp_codec_dev_handle_t codec = microphone_capture_get_codec();
  if (codec == NULL) {
    ESP_LOGE(TAG, "Microphone codec handle unavailable");
    microphone_capture_deinit();
    s_audio_failed = true;
    return ESP_ERR_INVALID_STATE;
  }

  esp_capture_audio_dev_src_cfg_t aud_cfg = {
    .record_handle = codec,
  };
  s_audio_src = esp_capture_new_audio_dev_src(&aud_cfg);
  if (s_audio_src == NULL) {
    ESP_LOGE(TAG, "Failed to create audio capture source");
    microphone_capture_deinit();
    s_audio_failed = true;
    return ESP_ERR_NO_MEM;
  }

  esp_audio_enc_register_default();
  s_audio_available = true;
  ESP_LOGI(TAG, "Microphone capture attached to WebRTC");
  return ESP_OK;
}
#endif

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
    .camera_ctrl_cb = webrtc_camera_ctrl_cb,  // Register pre-stream config callback
    .camera_ctrl_ctx = NULL,                   // No special context needed
  };

  s_video_src = esp_capture_new_video_v4l2_src(&cfg);
  if (s_video_src == NULL) {
    ESP_LOGE(TAG, "Failed to create V4L2 capture source");
    return ESP_ERR_NO_MEM;
  }

#if CONFIG_THEO_WEBRTC_FORCE_FIXED_CAPS
  if (s_video_src->set_fixed_caps) {
    esp_capture_video_info_t fixed_caps = {
      .format_id = ESP_CAPTURE_FMT_ID_O_UYY_E_VYY,
      .width = WEBRTC_FRAME_WIDTH,
      .height = WEBRTC_FRAME_HEIGHT,
      .fps = WEBRTC_FRAME_FPS,
    };
    esp_capture_err_t caps_err = s_video_src->set_fixed_caps(s_video_src, &fixed_caps);
    if (caps_err != ESP_CAPTURE_ERR_OK) {
      ESP_LOGW(TAG, "set_fixed_caps failed: %d", caps_err);
    } else {
      ESP_LOGI(TAG, "Fixed caps set: format=%d %dx%d@%d", fixed_caps.format_id, fixed_caps.width, fixed_caps.height, fixed_caps.fps);
    }
  } else {
    ESP_LOGW(TAG, "set_fixed_caps not supported by video source");
  }
#endif

#if CONFIG_THEO_MICROPHONE_ENABLE
  esp_err_t aud_err = ensure_microphone_ready();
  if (aud_err != ESP_OK) {
    ESP_LOGW(TAG, "Audio capture unavailable (%s)", esp_err_to_name(aud_err));
  }
#endif

  esp_capture_cfg_t capture_cfg = {
#if CONFIG_THEO_MICROPHONE_ENABLE
    .sync_mode = s_audio_available ? ESP_CAPTURE_SYNC_MODE_AUDIO : ESP_CAPTURE_SYNC_MODE_NONE,
    .video_src = s_video_src,
    .audio_src = s_audio_available ? s_audio_src : NULL,
#else
    .sync_mode = ESP_CAPTURE_SYNC_MODE_NONE,
    .video_src = s_video_src,
#endif
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

#if CONFIG_THEO_MICROPHONE_ENABLE
  if (s_audio_available) {
    sink_cfg.audio_info.format_id = ESP_CAPTURE_FMT_ID_OPUS;
    sink_cfg.audio_info.sample_rate = MICROPHONE_SAMPLE_RATE_HZ;
    sink_cfg.audio_info.channel = MICROPHONE_CHANNELS;
    sink_cfg.audio_info.bits_per_sample = 16;
  }
#endif

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

#if CONFIG_THEO_MICROPHONE_ENABLE
  if (s_audio_available) {
    const char *aud_elements[] = {"aud_ch_cvt", "aud_enc"};
    esp_capture_err_t aud_err = esp_capture_sink_build_pipeline(s_video_sink, ESP_CAPTURE_STREAM_TYPE_AUDIO,
                                                                aud_elements, sizeof(aud_elements) / sizeof(aud_elements[0]));
    if (aud_err != ESP_CAPTURE_ERR_OK) {
      ESP_LOGE(TAG, "Failed to build audio pipeline (%d)", aud_err);
      esp_capture_sink_disable_stream(s_video_sink, ESP_CAPTURE_STREAM_TYPE_AUDIO);
      s_audio_available = false;
    } else {
      esp_gmf_element_handle_t aud_src_el = NULL;
      esp_capture_err_t elem_err = esp_capture_sink_get_element_by_tag(s_video_sink,
                                                                       ESP_CAPTURE_STREAM_TYPE_AUDIO,
                                                                       "aud_src",
                                                                       &aud_src_el);
      if (elem_err == ESP_CAPTURE_ERR_OK && aud_src_el) {
        esp_gmf_err_t frame_err = capture_audio_src_el_set_in_frame_samples(aud_src_el, MICROPHONE_FRAME_SAMPLES);
        if (frame_err == ESP_GMF_ERR_OK) {
          ESP_LOGI(TAG, "Audio frame samples set to %d (%d ms blocks)",
                   MICROPHONE_FRAME_SAMPLES,
                   (MICROPHONE_FRAME_SAMPLES * 1000) / MICROPHONE_SAMPLE_RATE_HZ);
        } else {
          ESP_LOGW(TAG, "Failed to configure mic frame samples (%d)", frame_err);
        }
      } else {
        ESP_LOGW(TAG, "Unable to access aud_src element for frame tuning (%d)", elem_err);
      }
    }
  }
#endif

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
#if CONFIG_THEO_MICROPHONE_ENABLE
  if (s_audio_src) {
    s_audio_src->close(s_audio_src);
    s_audio_src = NULL;
  }
  microphone_capture_deinit();
  s_audio_available = false;
  s_audio_failed = false;
#endif
}

static void ensure_ir_led_ready(void)
{
  if (!s_ir_led_ready) {
    if (thermostat_ir_led_init() == ESP_OK) {
      s_ir_led_ready = true;
    }
  }
}

static void log_camera_stream_rate(void)
{
  int fd = open("/dev/video0", O_RDWR);
  if (fd < 0) {
    ESP_LOGW(TAG, "Failed to open /dev/video0 for fps query: %s", strerror(errno));
    return;
  }

  struct v4l2_streamparm parm = {0};
  parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if (ioctl(fd, VIDIOC_G_PARM, &parm) == 0 &&
      parm.parm.capture.timeperframe.numerator > 0 &&
      parm.parm.capture.timeperframe.denominator > 0) {
    float fps = (float)parm.parm.capture.timeperframe.denominator /
                (float)parm.parm.capture.timeperframe.numerator;
    ESP_LOGI(TAG, "Camera driver reports %.2f fps (tpf=%d/%d)",
             fps,
             parm.parm.capture.timeperframe.numerator,
             parm.parm.capture.timeperframe.denominator);
  } else {
    ESP_LOGW(TAG, "VIDIOC_G_PARM failed: %s", strerror(errno));
  }
  close(fd);
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
    ESP_LOGI(TAG, "Stopping WHEP stream");
    esp_webrtc_close(s_webrtc);
    s_webrtc = NULL;
  }
  if (s_ir_led_ready) {
    thermostat_ir_led_set(false);
  }
  whep_release_gate_if_idle();
}

static whep_request_t *whep_request_create(const char *stream_id, const char *offer, size_t offer_len)
{
  if (!offer || offer_len == 0) {
    return NULL;
  }

  whep_request_t *req = calloc(1, sizeof(*req));
  if (!req) {
    return NULL;
  }

  req->offer = malloc(offer_len);
  if (!req->offer) {
    whep_request_destroy(req);
    return NULL;
  }
  memcpy(req->offer, offer, offer_len);
  req->offer_len = offer_len;

  if (stream_id && stream_id[0] != '\0') {
    strlcpy(req->stream_id, stream_id, sizeof(req->stream_id));
  }

  req->done = xSemaphoreCreateBinary();
  if (!req->done) {
    whep_request_destroy(req);
    return NULL;
  }

  return req;
}

static void whep_request_destroy(whep_request_t *req)
{
  if (!req) {
    return;
  }
  free(req->offer);
  if (req->answer) {
    free(req->answer);
  }
  if (req->done) {
    vSemaphoreDelete(req->done);
  }
  free(req);
}

static void whep_answer_ready_cb(const uint8_t *data, size_t len, void *ctx)
{
  whep_request_t *req = (whep_request_t *)ctx;
  if (!req || !req->done) {
    return;
  }

  if (data && len > 0) {
    char *copy = malloc(len + 1);
    if (copy) {
      memcpy(copy, data, len);
      copy[len] = '\0';
      req->answer = copy;
      req->answer_len = len;
      req->status = ESP_OK;
    } else {
      req->status = ESP_ERR_NO_MEM;
    }
  } else {
    req->status = ESP_ERR_INVALID_STATE;
  }

  xSemaphoreGive(req->done);
}

static void whep_release_gate_if_idle(void)
{
  if (!s_state_mutex) {
    if (s_webrtc == NULL) {
      s_whep_session_gate = false;
    }
    return;
  }

  xSemaphoreTake(s_state_mutex, portMAX_DELAY);
  if (s_webrtc == NULL) {
    s_whep_session_gate = false;
  }
  xSemaphoreGive(s_state_mutex);
}

static esp_err_t start_webrtc_session_from_request(whep_request_t *req)
{
  if (!req) {
    return ESP_ERR_INVALID_ARG;
  }

  if (s_webrtc != NULL) {
    req->status = ESP_ERR_INVALID_STATE;
    if (req->done) {
      xSemaphoreGive(req->done);
    }
    return ESP_ERR_INVALID_STATE;
  }

  bool ready = s_module_running && s_wifi_ready;
  if (s_state_mutex) {
    xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    ready = s_module_running && s_wifi_ready;
    xSemaphoreGive(s_state_mutex);
  }
  if (!ready) {
    req->status = ESP_ERR_INVALID_STATE;
    if (req->done) {
      xSemaphoreGive(req->done);
    }
    return ESP_ERR_INVALID_STATE;
  }

  int64_t now = esp_timer_get_time();
  if (now < s_next_retry_time_us) {
    req->status = ESP_ERR_INVALID_STATE;
    if (req->done) {
      xSemaphoreGive(req->done);
    }
    return ESP_ERR_INVALID_STATE;
  }

  ensure_media_lib_ready();
  ESP_RETURN_ON_ERROR(ensure_camera_ready(), TAG, "camera init failed");

  bool audio_enabled = false;
#if CONFIG_THEO_MICROPHONE_ENABLE
  audio_enabled = s_audio_available;
#endif
  esp_peer_audio_codec_t audio_codec = audio_enabled ? ESP_PEER_AUDIO_CODEC_OPUS : ESP_PEER_AUDIO_CODEC_NONE;
  uint32_t audio_sample_rate = audio_enabled ? MICROPHONE_SAMPLE_RATE_HZ : 0;
  uint8_t audio_channel = audio_enabled ? MICROPHONE_CHANNELS : 0;
  esp_peer_media_dir_t audio_dir = audio_enabled ? ESP_PEER_MEDIA_DIR_SEND_ONLY : ESP_PEER_MEDIA_DIR_NONE;

  ESP_LOGI(TAG, "Configuring WebRTC: video=%dx%d@%d audio=%s",
           WEBRTC_FRAME_WIDTH,
           WEBRTC_FRAME_HEIGHT,
           WEBRTC_FRAME_FPS,
           audio_enabled ? "opus@16k" : "disabled");

  req->signal_cfg.offer = req->offer;
  req->signal_cfg.offer_len = req->offer_len;
  req->signal_cfg.on_answer = whep_answer_ready_cb;
  req->signal_cfg.ctx = req;

  esp_webrtc_cfg_t cfg = {
    .peer_cfg = {
      .audio_info = {
        .codec = audio_codec,
        .sample_rate = audio_sample_rate,
        .channel = audio_channel,
      },
      .video_info = {
        .codec = ESP_PEER_VIDEO_CODEC_H264,
        .width = WEBRTC_FRAME_WIDTH,
        .height = WEBRTC_FRAME_HEIGHT,
        .fps = WEBRTC_FRAME_FPS,
      },
      .audio_dir = audio_dir,
      .video_dir = ESP_PEER_MEDIA_DIR_SEND_ONLY,
      .ice_trans_policy = ESP_PEER_ICE_TRANS_POLICY_ALL,
      .enable_data_channel = false,
      .no_auto_reconnect = true,
    },
    .signaling_cfg = {
      .extra_cfg = &req->signal_cfg,
      .extra_size = sizeof(req->signal_cfg),
    },
    .peer_impl = esp_peer_get_default_impl(),
    .signaling_impl = esp_signaling_get_whep_impl(),
  };

  int ret = esp_webrtc_open(&cfg, &s_webrtc);
  if (ret != 0) {
    ESP_LOGE(TAG, "esp_webrtc_open failed (%d)", ret);
    s_webrtc = NULL;
    s_next_retry_time_us = now + WEBRTC_RETRY_DELAY_US;
    req->status = ESP_FAIL;
    if (req->done) {
      xSemaphoreGive(req->done);
    }
    return ESP_FAIL;
  }

#if CONFIG_THEO_MICROPHONE_ENABLE
  if (audio_enabled) {
    ret = esp_webrtc_set_audio_bitrate(s_webrtc, 32000);
    if (ret != ESP_PEER_ERR_NONE) {
      ESP_LOGW(TAG, "Failed to set audio bitrate (%d)", ret);
    }
  }
#endif
  ret = esp_webrtc_set_video_bitrate(s_webrtc, 10000000);
  if (ret != ESP_PEER_ERR_NONE) {
    ESP_LOGW(TAG, "Failed to set video bitrate (%d)", ret);
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
    req->status = ESP_FAIL;
    if (req->done) {
      xSemaphoreGive(req->done);
    }
    return ESP_FAIL;
  }

  log_internal_heap_state("After esp_webrtc_start", ESP_LOG_INFO, false);

  ESP_LOGI(TAG,
           "WHEP session negotiated (%s)",
           req->stream_id[0] ? req->stream_id : "anonymous");
  return ESP_OK;
}

static esp_err_t webrtc_handle_whep_request(const char *stream_id,
                                            const char *offer,
                                            size_t offer_len,
                                            char **answer_out,
                                            size_t *answer_len_out,
                                            void *ctx)
{
  (void)ctx;
  if (!offer || offer_len == 0 || !answer_out || !answer_len_out) {
    return ESP_ERR_INVALID_ARG;
  }

  whep_request_t *req = whep_request_create(stream_id, offer, offer_len);
  if (!req) {
    return ESP_ERR_NO_MEM;
  }

  if (!s_whep_request_queue) {
    whep_request_destroy(req);
    return ESP_ERR_INVALID_STATE;
  }

  xSemaphoreTake(s_state_mutex, portMAX_DELAY);
  bool wifi_ready = s_wifi_ready;
  bool gate_busy = s_whep_session_gate;
  int64_t now = esp_timer_get_time();
  bool retry_pending = now < s_next_retry_time_us;
  if (!wifi_ready || gate_busy || retry_pending) {
    xSemaphoreGive(s_state_mutex);
    whep_request_destroy(req);
    return ESP_ERR_INVALID_STATE;
  }
  s_whep_session_gate = true;
  xSemaphoreGive(s_state_mutex);

  if (xQueueSend(s_whep_request_queue, &req, portMAX_DELAY) != pdPASS) {
    whep_request_destroy(req);
    whep_release_gate_if_idle();
    return ESP_FAIL;
  }

  request_task_event(WEBRTC_TASK_EVENT_START);

  if (xSemaphoreTake(req->done, portMAX_DELAY) != pdTRUE) {
    whep_request_destroy(req);
    whep_release_gate_if_idle();
    return ESP_ERR_TIMEOUT;
  }

  esp_err_t status = req->status;
  if (status == ESP_OK && req->answer && req->answer_len > 0) {
    *answer_out = req->answer;
    *answer_len_out = req->answer_len;
    req->answer = NULL;
    req->answer_len = 0;
  } else if (status == ESP_OK) {
    status = ESP_ERR_INVALID_STATE;
  }

  if (status != ESP_OK) {
    whep_release_gate_if_idle();
  }

  whep_request_destroy(req);
  return status;
}

static void webrtc_task(void *arg)
{
  while (true) {
    uint32_t events = 0;
    xTaskNotifyWait(0, UINT32_MAX, &events, pdMS_TO_TICKS(500));

    if (events & WEBRTC_TASK_EVENT_STOP) {
      teardown_webrtc();
    }

    if ((events & WEBRTC_TASK_EVENT_RESTART) != 0) {
      teardown_webrtc();
    }

    if (s_whep_request_queue) {
      whep_request_t *pending = NULL;
      if (xQueueReceive(s_whep_request_queue, &pending, 0) == pdPASS && pending) {
        start_webrtc_session_from_request(pending);
      }
    }

    if (s_webrtc) {
      int64_t now = esp_timer_get_time();
      if (now >= s_next_query_time_us) {
        esp_webrtc_query(s_webrtc);
        s_next_query_time_us = now + WEBRTC_QUERY_INTERVAL_US;
      }

      if (now >= s_next_fps_log_time_us) {
        log_camera_stream_rate();
        s_next_fps_log_time_us = now + CAMERA_FPS_LOG_INTERVAL_US;
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

  if (!s_peer_logs_tamed) {
    esp_log_level_set("PEER_DEF", ESP_LOG_ERROR);
    s_peer_logs_tamed = true;
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
  s_next_fps_log_time_us = esp_timer_get_time() + CAMERA_FPS_LOG_INTERVAL_US;
  xSemaphoreGive(s_state_mutex);

  if (!s_whep_request_queue) {
    s_whep_request_queue = xQueueCreate(1, sizeof(whep_request_t *));
    if (!s_whep_request_queue) {
      ESP_LOGE(TAG, "Failed to allocate WHEP request queue");
      xSemaphoreTake(s_state_mutex, portMAX_DELAY);
      s_module_running = false;
      xSemaphoreGive(s_state_mutex);
      return ESP_ERR_NO_MEM;
    }
  }

  if (!s_whep_endpoint_registered) {
    esp_err_t whep_err = whep_endpoint_start(webrtc_handle_whep_request, NULL);
    if (whep_err != ESP_OK) {
      ESP_LOGE(TAG, "Failed to register WHEP endpoint: %s", esp_err_to_name(whep_err));
      return whep_err;
    }
    s_whep_endpoint_registered = true;
  }

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

  if (s_whep_request_queue)
  {
    whep_request_t *pending = NULL;
    while (xQueueReceive(s_whep_request_queue, &pending, 0) == pdPASS)
    {
      if (pending && pending->done)
      {
        pending->status = ESP_ERR_INVALID_STATE;
        xSemaphoreGive(pending->done);
      }
    }
  }

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
