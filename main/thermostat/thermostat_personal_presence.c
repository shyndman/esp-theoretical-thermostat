#include "thermostat/thermostat_personal_presence.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/timers.h"
#include "thermostat/audio_personal.h"
#include "thermostat/thermostat_led_status.h"

#define PERSONAL_FACE_MAX_LEN (47)
#define GREETING_TIMEOUT_MS   (1200)

typedef struct {
  char face[PERSONAL_FACE_MAX_LEN + 1];
  bool initial_face_consumed;
  bool person_count_valid;
  int person_count;
  bool cue_active;
  bool timer_armed;
  int64_t last_trigger_us;
  TimerHandle_t cue_timer;
} personal_presence_state_t;

static personal_presence_state_t s_presence;
static portMUX_TYPE s_presence_lock = portMUX_INITIALIZER_UNLOCKED;
static const char *TAG = "personal_presence";

static void greeting_timer_cb(TimerHandle_t timer);
static bool ensure_timer_created(void);
static bool start_greeting_timer(void);
static void finish_greeting(const char *reason, bool stop_timer);
static void trigger_greeting(void);
static void copy_face_payload(char *dest, size_t len, const char *payload);
static void copy_trimmed(char *dest, size_t len, const char *payload);

void thermostat_personal_presence_init(void)
{
  ensure_timer_created();
}

void thermostat_personal_presence_process_person_count(const char *payload)
{
  char buffer[32];
  copy_trimmed(buffer, sizeof(buffer), payload);

  if (buffer[0] == '\0' || strcasecmp(buffer, "unavailable") == 0)
  {
    ESP_LOGW(TAG, "Person count unavailable; suppressing greetings until numeric payload returns");
    portENTER_CRITICAL(&s_presence_lock);
    s_presence.person_count_valid = false;
    portEXIT_CRITICAL(&s_presence_lock);
    return;
  }

  errno = 0;
  char *end = NULL;
  long value = strtol(buffer, &end, 10);
  if (errno != 0 || end == buffer || (end && *end != '\0') || value < 0 || value > INT_MAX)
  {
    ESP_LOGW(TAG, "Invalid person count payload (%s)", buffer);
    portENTER_CRITICAL(&s_presence_lock);
    s_presence.person_count_valid = false;
    portEXIT_CRITICAL(&s_presence_lock);
    return;
  }

  portENTER_CRITICAL(&s_presence_lock);
  s_presence.person_count = (int)value;
  s_presence.person_count_valid = true;
  portEXIT_CRITICAL(&s_presence_lock);

  ESP_LOGI(TAG, "Person count updated: %ld", value);
}

void thermostat_personal_presence_process_face(const char *payload, bool retained)
{
  char face[PERSONAL_FACE_MAX_LEN + 1];
  copy_face_payload(face, sizeof(face), payload);

  bool first_payload = false;
  bool should_trigger = false;
  int cached_count = 0;

  portENTER_CRITICAL(&s_presence_lock);
  if (!s_presence.initial_face_consumed)
  {
    s_presence.initial_face_consumed = true;
    first_payload = true;
  }

  if (first_payload && retained)
  {
    portEXIT_CRITICAL(&s_presence_lock);
    ESP_LOGI(TAG, "Retained face payload ignored (%s)", face[0] ? face : "<empty>");
    return;
  }

  if (strncmp(s_presence.face, face, sizeof(s_presence.face)) != 0)
  {
    strlcpy(s_presence.face, face, sizeof(s_presence.face));
  }

  if (strcmp(face, "Scott") != 0)
  {
    portEXIT_CRITICAL(&s_presence_lock);
    ESP_LOGD(TAG, "Face payload (%s) ignored", face[0] ? face : "<empty>");
    return;
  }

  if (!s_presence.person_count_valid)
  {
    portEXIT_CRITICAL(&s_presence_lock);
    ESP_LOGI(TAG, "Scott recognized but person count invalid; waiting for numeric payload");
    return;
  }

  if (s_presence.person_count < 1)
  {
    cached_count = s_presence.person_count;
    portEXIT_CRITICAL(&s_presence_lock);
    ESP_LOGI(TAG, "Scott recognized but count=%d; greeting suppressed", cached_count);
    return;
  }

  if (s_presence.cue_active)
  {
    portEXIT_CRITICAL(&s_presence_lock);
    ESP_LOGD(TAG, "Scott payload dropped; greeting already active");
    return;
  }

  s_presence.cue_active = true;
  s_presence.timer_armed = false;
  s_presence.last_trigger_us = esp_timer_get_time();
  cached_count = s_presence.person_count;
  should_trigger = true;
  portEXIT_CRITICAL(&s_presence_lock);

  if (!should_trigger)
  {
    return;
  }

  ESP_LOGI(TAG, "Scott recognized (count=%d); launching greeting", cached_count);
  trigger_greeting();
}

void thermostat_personal_presence_on_led_complete(void)
{
  finish_greeting("LED complete", true);
}

static void trigger_greeting(void)
{
  esp_err_t audio_err = thermostat_audio_personal_play_scott();
  if (audio_err != ESP_OK)
  {
    ESP_LOGW(TAG, "Scott greeting audio failed: %s", esp_err_to_name(audio_err));
  }

  esp_err_t led_err = thermostat_led_status_trigger_greeting();
  if (led_err != ESP_OK)
  {
    ESP_LOGW(TAG, "Scott greeting LEDs unavailable: %s", esp_err_to_name(led_err));
    finish_greeting("LED rejected", false);
    return;
  }

  if (!start_greeting_timer())
  {
    ESP_LOGW(TAG, "Greeting timer unavailable; clearing immediately");
    finish_greeting("timer unavailable", false);
  }
}

static bool ensure_timer_created(void)
{
  if (s_presence.cue_timer)
  {
    return true;
  }

  s_presence.cue_timer = xTimerCreate(
      "pp_greet",
      pdMS_TO_TICKS(GREETING_TIMEOUT_MS),
      pdFALSE,
      NULL,
      greeting_timer_cb);
  if (!s_presence.cue_timer)
  {
    ESP_LOGW(TAG, "Failed to allocate personal presence timer");
    return false;
  }
  return true;
}

static bool start_greeting_timer(void)
{
  if (!ensure_timer_created())
  {
    return false;
  }

  if (xTimerStop(s_presence.cue_timer, 0) != pdPASS)
  {
    ESP_LOGW(TAG, "Failed to stop greeting timer");
  }
  if (xTimerChangePeriod(s_presence.cue_timer, pdMS_TO_TICKS(GREETING_TIMEOUT_MS), 0) != pdPASS)
  {
    ESP_LOGW(TAG, "Failed to arm greeting timer");
    return false;
  }
  if (xTimerStart(s_presence.cue_timer, 0) != pdPASS)
  {
    ESP_LOGW(TAG, "Greeting timer start failed");
    return false;
  }

  portENTER_CRITICAL(&s_presence_lock);
  s_presence.timer_armed = true;
  portEXIT_CRITICAL(&s_presence_lock);
  return true;
}

static void greeting_timer_cb(TimerHandle_t timer)
{
  (void)timer;
  finish_greeting("timer elapsed", false);
}

static void finish_greeting(const char *reason, bool stop_timer)
{
  TimerHandle_t timer_to_stop = NULL;
  int64_t elapsed_ms = 0;

  portENTER_CRITICAL(&s_presence_lock);
  if (!s_presence.cue_active)
  {
    portEXIT_CRITICAL(&s_presence_lock);
    return;
  }
  s_presence.cue_active = false;
  if (s_presence.timer_armed)
  {
    timer_to_stop = s_presence.cue_timer;
  }
  s_presence.timer_armed = false;
  elapsed_ms = (esp_timer_get_time() - s_presence.last_trigger_us) / 1000;
  portEXIT_CRITICAL(&s_presence_lock);

  if (stop_timer && timer_to_stop)
  {
    xTimerStop(timer_to_stop, 0);
  }

  ESP_LOGI(TAG, "Scott greeting complete via %s (%lld ms)", reason, (long long)elapsed_ms);
}

static void copy_face_payload(char *dest, size_t len, const char *payload)
{
  if (!dest || len == 0)
  {
    return;
  }
  if (!payload)
  {
    dest[0] = '\0';
    return;
  }
  strlcpy(dest, payload, len);
  size_t current = strlen(dest);
  while (current > 0 && (dest[current - 1] == '\n' || dest[current - 1] == '\r'))
  {
    dest[current - 1] = '\0';
    --current;
  }
}

static void copy_trimmed(char *dest, size_t len, const char *payload)
{
  if (!dest || len == 0)
  {
    return;
  }
  dest[0] = '\0';
  if (!payload)
  {
    return;
  }
  strlcpy(dest, payload, len);
  size_t start = 0;
  size_t end = strlen(dest);
  while (start < end && isspace((unsigned char)dest[start]))
  {
    ++start;
  }
  while (end > start && isspace((unsigned char)dest[end - 1]))
  {
    --end;
  }
  size_t idx = 0;
  while (start < end && idx < len - 1)
  {
    dest[idx++] = dest[start++];
  }
  dest[idx] = '\0';
}
