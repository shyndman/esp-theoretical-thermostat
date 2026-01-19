#include "streaming_state.h"

#include <string.h>

#include "freertos/semphr.h"

typedef struct {
  bool video_client_active;
  bool audio_client_active;
  bool video_pipeline_active;
  bool audio_pipeline_active;
  bool video_failed;
  bool audio_failed;
  int stream_refcount;
} streaming_state_t;

static SemaphoreHandle_t s_stream_mutex = NULL;
static streaming_state_t s_state = {0};

esp_err_t streaming_state_init(void)
{
  if (s_stream_mutex == NULL) {
    s_stream_mutex = xSemaphoreCreateMutex();
    if (s_stream_mutex == NULL) {
      return ESP_ERR_NO_MEM;
    }
  }

  memset(&s_state, 0, sizeof(s_state));
  return ESP_OK;
}

void streaming_state_deinit(void)
{
  if (s_stream_mutex) {
    vSemaphoreDelete(s_stream_mutex);
    s_stream_mutex = NULL;
  }

  memset(&s_state, 0, sizeof(s_state));
}

bool streaming_state_lock(TickType_t timeout)
{
  if (s_stream_mutex == NULL) {
    return false;
  }

  return xSemaphoreTake(s_stream_mutex, timeout) == pdTRUE;
}

void streaming_state_unlock(void)
{
  if (s_stream_mutex) {
    xSemaphoreGive(s_stream_mutex);
  }
}

bool streaming_state_video_client_active(void)
{
  return s_state.video_client_active;
}

bool streaming_state_audio_client_active(void)
{
  return s_state.audio_client_active;
}

bool streaming_state_video_pipeline_active(void)
{
  return s_state.video_pipeline_active;
}

bool streaming_state_audio_pipeline_active(void)
{
  return s_state.audio_pipeline_active;
}

bool streaming_state_video_failed(void)
{
  return s_state.video_failed;
}

bool streaming_state_audio_failed(void)
{
  return s_state.audio_failed;
}

void streaming_state_set_video_client_active(bool active)
{
  s_state.video_client_active = active;
}

void streaming_state_set_audio_client_active(bool active)
{
  s_state.audio_client_active = active;
}

void streaming_state_set_video_pipeline_active(bool active)
{
  s_state.video_pipeline_active = active;
}

void streaming_state_set_audio_pipeline_active(bool active)
{
  s_state.audio_pipeline_active = active;
}

void streaming_state_set_video_failed(bool failed)
{
  s_state.video_failed = failed;
}

void streaming_state_set_audio_failed(bool failed)
{
  s_state.audio_failed = failed;
}

int streaming_state_increment_refcount(void)
{
  s_state.stream_refcount += 1;
  return s_state.stream_refcount;
}

int streaming_state_decrement_refcount(void)
{
  if (s_state.stream_refcount > 0) {
    s_state.stream_refcount -= 1;
  }
  return s_state.stream_refcount;
}

int streaming_state_refcount(void)
{
  return s_state.stream_refcount;
}
