#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"

esp_err_t streaming_state_init(void);
void streaming_state_deinit(void);
bool streaming_state_lock(TickType_t timeout);
void streaming_state_unlock(void);

bool streaming_state_video_client_active(void);
bool streaming_state_audio_client_active(void);
bool streaming_state_video_pipeline_active(void);
bool streaming_state_audio_pipeline_active(void);
bool streaming_state_video_failed(void);
bool streaming_state_audio_failed(void);

void streaming_state_set_video_client_active(bool active);
void streaming_state_set_audio_client_active(bool active);
void streaming_state_set_video_pipeline_active(bool active);
void streaming_state_set_audio_pipeline_active(bool active);
void streaming_state_set_video_failed(bool failed);
void streaming_state_set_audio_failed(bool failed);

int streaming_state_increment_refcount(void);
int streaming_state_decrement_refcount(void);
int streaming_state_refcount(void);
