#include "mjpeg_stream.h"

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <errno.h>

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
#include "driver/i2c_master.h"
#include "driver/jpeg_encode.h"

#define MJPEG_DESIRED_PIXFORMAT V4L2_PIX_FMT_YUV422P

static const char *TAG = "mjpeg_stream";

#define FRAME_WIDTH   1280
#define FRAME_HEIGHT  960
#define CAM_BUF_COUNT 2
#define STREAM_MAX_OPEN_SOCKETS 2
#define HTTPD_INTERNAL_SOCKETS 3
#define STREAM_REQUIRED_LWIP_SOCKETS (STREAM_MAX_OPEN_SOCKETS + HTTPD_INTERNAL_SOCKETS)
#define VIDEO_STREAM_TASK_STACK 8192
#define VIDEO_STREAM_TASK_PRIORITY 5
#define JPEG_OUTPUT_BUFFER_SIZE (1024 * 1024) // 1MB
#define JPEG_QUALITY 80

#define STREAM_BOUNDARY "123456789000000000000987654321"
#define STREAM_PART_HEADER "Content-Type: image/jpeg\r\nContent-Length: %zu\r\n\r\n"
#define STREAM_PART_FOOTER "\r\n"

#if defined(CONFIG_LWIP_MAX_SOCKETS) && CONFIG_LWIP_MAX_SOCKETS < STREAM_REQUIRED_LWIP_SOCKETS
#error "CONFIG_LWIP_MAX_SOCKETS must be >= STREAM_REQUIRED_LWIP_SOCKETS"
#endif

typedef struct {
  void *start;
  size_t length;
} buffer_t;

static esp_ldo_channel_handle_t s_ldo_mipi_phy = NULL;
static int s_cam_fd = -1;
static buffer_t s_cam_buffers[CAM_BUF_COUNT];
static bool s_video_initialized = false;
static jpeg_encoder_handle_t s_jpeg_encoder = NULL;
static uint8_t *s_jpeg_output_buffer = NULL;
static size_t s_jpeg_output_buffer_size = 0;
static size_t s_cam_frame_bytes = 0;
static httpd_handle_t s_httpd = NULL;
static uint32_t s_cam_pixel_format = MJPEG_DESIRED_PIXFORMAT;
static uint32_t s_cam_bytesperline = FRAME_WIDTH;

// Forward declarations
static esp_err_t acquire_mipi_phy_ldo(void);
static void release_mipi_phy_ldo(void);
static esp_err_t init_video_framework(void);
static esp_err_t configure_v4l2_capture(void);
static esp_err_t configure_camera_flip(void);
static esp_err_t configure_capture_buffers(void);
static esp_err_t init_jpeg_encoder(void);
static void video_stream_task(void *pvParameters);
static esp_err_t video_stream_handler(httpd_req_t *req);
static void cleanup_resources(void);
static esp_err_t start_http_server(void);
static void fourcc_to_str(uint32_t fourcc, char out[5]);

static esp_err_t acquire_mipi_phy_ldo(void)
{
    ESP_LOGI(TAG, "Acquiring MIPI PHY LDO channel 3");
    
    esp_ldo_channel_config_t ldo_cfg = {
        .chan_id = 3,
        .voltage_mv = 2500,
    };

    esp_err_t ret = esp_ldo_acquire_channel(&ldo_cfg, &s_ldo_mipi_phy);
    if (ret == ESP_ERR_INVALID_STATE) {
        ESP_LOGI(TAG, "MIPI PHY LDO already enabled; skipping acquire");
        s_ldo_mipi_phy = NULL;
        return ESP_OK;
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to acquire LDO channel: %s", esp_err_to_name(ret));
        return ret;
    }
    
    return ESP_OK;
}

static void release_mipi_phy_ldo(void)
{
    if (s_ldo_mipi_phy != NULL) {
        ESP_LOGI(TAG, "Releasing MIPI PHY LDO channel 3");
        esp_ldo_release_channel(s_ldo_mipi_phy);
        s_ldo_mipi_phy = NULL;
    }
}

static esp_err_t init_video_framework(void)
{
    ESP_LOGI(TAG, "Initializing video framework");
    
    i2c_master_bus_handle_t i2c_handle = bsp_i2c_get_handle();
    if (i2c_handle == NULL) {
        ESP_LOGE(TAG, "bsp_i2c_get_handle returned NULL");
        return ESP_ERR_INVALID_STATE;
    }
    
    esp_video_init_csi_config_t csi_cfg = {
        .sccb_config = {
            .init_sccb = false,
            .i2c_handle = i2c_handle,
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
        ESP_LOGE(TAG, "Failed to init esp_video: %s", esp_err_to_name(err));
        return err;
    }
    
    s_video_initialized = true;
    ESP_LOGI(TAG, "Video framework initialized successfully");
    return ESP_OK;
}

static esp_err_t configure_v4l2_capture(void)
{
    ESP_LOGI(TAG, "Opening V4L2 device /dev/video0");
    
    s_cam_fd = open("/dev/video0", O_RDWR);
    if (s_cam_fd < 0) {
        ESP_LOGE(TAG, "Failed to open /dev/video0: %s", strerror(errno));
        return ESP_ERR_NOT_FOUND;
    }

    struct v4l2_format fmt = {
        .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
        .fmt.pix = {
            .width = FRAME_WIDTH,
            .height = FRAME_HEIGHT,
            .pixelformat = MJPEG_DESIRED_PIXFORMAT,
        },
    };

    if (ioctl(s_cam_fd, VIDIOC_S_FMT, &fmt) < 0) {
        ESP_LOGE(TAG, "Failed to set capture format");
        return ESP_FAIL;
    }

    s_cam_pixel_format = fmt.fmt.pix.pixelformat;

    if (s_cam_pixel_format != MJPEG_DESIRED_PIXFORMAT) {
        char requested_fmt[5];
        char actual_fmt[5];
        fourcc_to_str(MJPEG_DESIRED_PIXFORMAT, requested_fmt);
        fourcc_to_str(s_cam_pixel_format, actual_fmt);
        ESP_LOGW(TAG, "Camera returned %s instead of requested %s", actual_fmt, requested_fmt);
    }

    char active_fmt[5];
    fourcc_to_str(s_cam_pixel_format, active_fmt);
    ESP_LOGI(TAG, "Capture format %ux%u %s",
             fmt.fmt.pix.width,
             fmt.fmt.pix.height,
             active_fmt);

    s_cam_bytesperline = fmt.fmt.pix.bytesperline;
    s_cam_frame_bytes = fmt.fmt.pix.sizeimage;
    ESP_LOGI(TAG, "bytesperline=%u sizeimage=%zu", s_cam_bytesperline, (size_t)s_cam_frame_bytes);

    struct v4l2_streamparm parm = {
        .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
        .parm.capture.capability = V4L2_CAP_TIMEPERFRAME,
        .parm.capture.timeperframe = {
            .numerator = 1,
            .denominator = CONFIG_THEO_MJPEG_STREAM_FPS,
        },
    };
    if (ioctl(s_cam_fd, VIDIOC_S_PARM, &parm) < 0) {
        ESP_LOGW(TAG, "Failed to set frame rate to %d FPS", CONFIG_THEO_MJPEG_STREAM_FPS);
    }

    if (ioctl(s_cam_fd, VIDIOC_G_PARM, &parm) == 0) {
        ESP_LOGI(TAG, "Active FPS %u/%u",
                 parm.parm.capture.timeperframe.denominator,
                 parm.parm.capture.timeperframe.numerator);
    }

    return ESP_OK;
}

static esp_err_t configure_camera_flip(void)
{
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
    }
    return ESP_OK;
}

static esp_err_t configure_capture_buffers(void)
{
    struct v4l2_requestbuffers req = {
        .count = CAM_BUF_COUNT,
        .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
        .memory = V4L2_MEMORY_MMAP,
    };
    
    if (ioctl(s_cam_fd, VIDIOC_REQBUFS, &req) < 0) {
        ESP_LOGE(TAG, "Failed to request capture buffers: %s", strerror(errno));
        return ESP_FAIL;
    }
    
    for (int i = 0; i < CAM_BUF_COUNT; i++) {
        struct v4l2_buffer buf = {
            .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
            .memory = V4L2_MEMORY_MMAP,
            .index = i,
        };
        
        if (ioctl(s_cam_fd, VIDIOC_QUERYBUF, &buf) < 0) {
            ESP_LOGE(TAG, "Failed to query buffer %d", i);
            return ESP_FAIL;
        }
        
        s_cam_buffers[i].length = buf.length;
        s_cam_buffers[i].start = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, s_cam_fd, buf.m.offset);
        if (s_cam_buffers[i].start == MAP_FAILED) {
            ESP_LOGE(TAG, "Failed to map buffer %d", i);
            return ESP_FAIL;
        }
        
        if (ioctl(s_cam_fd, VIDIOC_QBUF, &buf) < 0) {
            ESP_LOGE(TAG, "Failed to queue buffer %d", i);
            return ESP_FAIL;
        }
    }
    
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(s_cam_fd, VIDIOC_STREAMON, &type) < 0) {
        ESP_LOGE(TAG, "Failed to start capture stream");
        return ESP_FAIL;
    }

    return ESP_OK;
}

static esp_err_t init_jpeg_encoder(void)
{
    jpeg_encode_engine_cfg_t engine_cfg = {
        .timeout_ms = 100,
    };

    esp_err_t ret = jpeg_new_encoder_engine(&engine_cfg, &s_jpeg_encoder);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create JPEG encoder engine: %s", esp_err_to_name(ret));
        return ret;
    }

    size_t requested = JPEG_OUTPUT_BUFFER_SIZE;
    if (s_cam_frame_bytes) {
        size_t estimate = (s_cam_frame_bytes * 3) / 4;
        if (estimate > requested) {
            requested = estimate;
        }
    }

    jpeg_encode_memory_alloc_cfg_t mem_cfg = {
        .buffer_direction = JPEG_ENC_ALLOC_OUTPUT_BUFFER,
    };
    size_t allocated = 0;
    s_jpeg_output_buffer = (uint8_t *)jpeg_alloc_encoder_mem(requested, &mem_cfg, &allocated);
    if (!s_jpeg_output_buffer) {
        ESP_LOGE(TAG, "Failed to allocate JPEG output buffer (%zu bytes)", requested);
        jpeg_del_encoder_engine(s_jpeg_encoder);
        s_jpeg_encoder = NULL;
        return ESP_ERR_NO_MEM;
    }

    s_jpeg_output_buffer_size = allocated;
    ESP_LOGI(TAG, "Allocated %zu-byte JPEG buffer", s_jpeg_output_buffer_size);

    return ESP_OK;
}

static void video_stream_task(void *pvParameters)
{
    httpd_req_t *async_req = (httpd_req_t *)pvParameters;
    esp_err_t res = ESP_OK;
    char *part_buf = malloc(128);

    ESP_LOGI(TAG, "Starting video stream task");
    
    streaming_state_lock(portMAX_DELAY);
    bool first_client = (streaming_state_increment_refcount() == 1);
    if (first_client) {
        thermostat_ir_led_set(true);
        streaming_state_set_video_client_active(true);
    }
    streaming_state_unlock();

    httpd_resp_set_type(async_req, "multipart/x-mixed-replace;boundary=" STREAM_BOUNDARY);

    jpeg_encode_cfg_t encode_cfg = {
        .src_type = JPEG_ENCODE_IN_FORMAT_YUV422,
        .sub_sample = JPEG_DOWN_SAMPLING_YUV422,
        .image_quality = JPEG_QUALITY,
        .width = FRAME_WIDTH,
        .height = FRAME_HEIGHT,
    };

    bool first_chunk = true;

    while (res == ESP_OK) {
        struct v4l2_buffer buf = {
            .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
            .memory = V4L2_MEMORY_MMAP,
        };

        if (ioctl(s_cam_fd, VIDIOC_DQBUF, &buf) < 0) {
            ESP_LOGE(TAG, "Failed to dequeue buffer");
            break;
        }

        uint32_t out_size = 0;
        res = jpeg_encoder_process(s_jpeg_encoder, &encode_cfg,
                                   s_cam_buffers[buf.index].start,
                                   buf.bytesused,
                                   s_jpeg_output_buffer,
                                   s_jpeg_output_buffer_size,
                                   &out_size);

        if (res == ESP_OK) {
            size_t hlen;
            if (first_chunk) {
                hlen = snprintf(part_buf, 128, "--%s\r\n", STREAM_BOUNDARY);
                first_chunk = false;
            } else {
                hlen = snprintf(part_buf, 128, "\r\n--%s\r\n", STREAM_BOUNDARY);
            }
            res = httpd_resp_send_chunk(async_req, part_buf, hlen);

            if (res == ESP_OK) {
                hlen = snprintf(part_buf, 128, STREAM_PART_HEADER, (size_t)out_size);
                res = httpd_resp_send_chunk(async_req, part_buf, hlen);
            }
            if (res == ESP_OK) {
                res = httpd_resp_send_chunk(async_req, (const char *)s_jpeg_output_buffer, out_size);
            }
            if (res == ESP_OK) {
                res = httpd_resp_send_chunk(async_req, STREAM_PART_FOOTER, strlen(STREAM_PART_FOOTER));
            }
        }

        if (ioctl(s_cam_fd, VIDIOC_QBUF, &buf) < 0) {
            ESP_LOGE(TAG, "Failed to queue buffer");
        }

        if (res != ESP_OK) {
            ESP_LOGI(TAG, "Client disconnected or error occurred");
            break;
        }
    }

    free(part_buf);
    httpd_resp_send_chunk(async_req, NULL, 0);
    httpd_req_async_handler_complete(async_req);
    
    streaming_state_lock(portMAX_DELAY);
    if (streaming_state_decrement_refcount() == 0) {
        thermostat_ir_led_set(false);
        streaming_state_set_video_client_active(false);
    }
    streaming_state_unlock();

    ESP_LOGI(TAG, "Video stream task finished");
    vTaskDelete(NULL);
}

static esp_err_t video_stream_handler(httpd_req_t *req)
{
    httpd_req_t *async_req = NULL;
    esp_err_t ret = httpd_req_async_handler_begin(req, &async_req);
    if (ret != ESP_OK) {
        return ret;
    }

    if (xTaskCreatePinnedToCore(video_stream_task, "video_stream", VIDEO_STREAM_TASK_STACK, async_req, VIDEO_STREAM_TASK_PRIORITY, NULL, 1) != pdPASS) {
        httpd_req_async_handler_complete(async_req);
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

static void cleanup_resources(void)
{
    if (s_cam_fd >= 0) {
        enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        ioctl(s_cam_fd, VIDIOC_STREAMOFF, &type);
        
        for (int i = 0; i < CAM_BUF_COUNT; i++) {
            if (s_cam_buffers[i].start && s_cam_buffers[i].start != MAP_FAILED) {
                munmap(s_cam_buffers[i].start, s_cam_buffers[i].length);
                s_cam_buffers[i].start = NULL;
            }
        }
        close(s_cam_fd);
        s_cam_fd = -1;
    }

    if (s_jpeg_encoder) {
        jpeg_del_encoder_engine(s_jpeg_encoder);
        s_jpeg_encoder = NULL;
    }

    if (s_jpeg_output_buffer) {
        free(s_jpeg_output_buffer);
        s_jpeg_output_buffer = NULL;
        s_jpeg_output_buffer_size = 0;
    }

    if (s_video_initialized) {
        s_video_initialized = false;
    }

    if (s_httpd) {
        httpd_stop(s_httpd);
        s_httpd = NULL;
    }

    release_mipi_phy_ldo();
}

static esp_err_t start_http_server(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = CONFIG_THEO_MJPEG_STREAM_PORT;
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
        .handler = video_stream_handler,
        .user_ctx = NULL,
    };

    err = httpd_register_uri_handler(s_httpd, &stream_uri);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register video handler: %s", esp_err_to_name(err));
        httpd_stop(s_httpd);
        s_httpd = NULL;
        return err;
    }

    ESP_LOGI(TAG, "HTTP server started on port %d", CONFIG_THEO_MJPEG_STREAM_PORT);
    return ESP_OK;
}

esp_err_t mjpeg_stream_start(void)
{
    ESP_LOGI(TAG, "Starting MJPEG stream server...");
    
    esp_err_t ret = streaming_state_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize streaming state: %s", esp_err_to_name(ret));
        return ret;
    }

    thermostat_ir_led_init();

    ret = acquire_mipi_phy_ldo();
    if (ret != ESP_OK) goto fail;
    
    ret = init_video_framework();
    if (ret != ESP_OK) goto fail;
    
    ret = configure_v4l2_capture();
    if (ret != ESP_OK) goto fail;
    
    ret = configure_camera_flip();
    if (ret != ESP_OK) goto fail;
    
    ret = configure_capture_buffers();
    if (ret != ESP_OK) goto fail;
    
    ret = init_jpeg_encoder();
    if (ret != ESP_OK) goto fail;

    ret = start_http_server();
    if (ret != ESP_OK) goto fail;

    char ip_addr[WIFI_REMOTE_MANAGER_IPV4_STR_LEN] = {0};
    if (wifi_remote_manager_get_sta_ip(ip_addr, sizeof(ip_addr)) == ESP_OK) {
        ESP_LOGI(TAG, "MJPEG stream available at http://%s:%d/video",
                 ip_addr, CONFIG_THEO_MJPEG_STREAM_PORT);
    } else {
        ESP_LOGI(TAG, "MJPEG stream available at http://<ip>:%d/video",
                 CONFIG_THEO_MJPEG_STREAM_PORT);
    }
    
    return ESP_OK;

fail:
    cleanup_resources();
    streaming_state_deinit();
    return ret;
}

void mjpeg_stream_stop(void)
{
    ESP_LOGI(TAG, "Stopping MJPEG stream");
    cleanup_resources();
    streaming_state_deinit();
}

static void fourcc_to_str(uint32_t fourcc, char out[5])
{
    out[0] = (char)(fourcc & 0xFF);
    out[1] = (char)((fourcc >> 8) & 0xFF);
    out[2] = (char)((fourcc >> 16) & 0xFF);
    out[3] = (char)((fourcc >> 24) & 0xFF);
    out[4] = '\0';
}
