#include "sensors/env_sensors.h"

#include <stdio.h>
#include <string.h>

#include "aht.h"
#include "bmp280.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "i2cdev.h"
#include "sdkconfig.h"

#include "connectivity/mqtt_manager.h"
#include "driver/i2c_master.h"
#include "mqtt_client.h"

#define ENV_TASK_STACK        (4096)
#define ENV_TASK_PRIORITY     (5)
#define ENV_TOPIC_MAX_LEN     (192)
#define ENV_DISCOVERY_PREFIX  "homeassistant"
#define ENV_MANUFACTURER      "YourOrg"
#define ENV_MODEL             "Theostat v1"
#define ENV_I2C_PORT          I2C_NUM_0

static const char *TAG = "env_sensors";

typedef enum {
  SENSOR_SRC_BMP = 0,
  SENSOR_SRC_AHT,
} sensor_source_t;

typedef enum {
  SENSOR_CH_TEMP_BMP = 0,
  SENSOR_CH_TEMP_AHT,
  SENSOR_CH_HUMIDITY,
  SENSOR_CH_PRESSURE,
  SENSOR_CH_COUNT,
} sensor_channel_t;

typedef struct {
  const char *object_id;
  const char *device_class;
  const char *unit;
  sensor_source_t source;
} sensor_channel_desc_t;

typedef struct {
  sensor_channel_desc_t desc;
  char state_topic[ENV_TOPIC_MAX_LEN];
  char availability_topic[ENV_TOPIC_MAX_LEN];
  char discovery_topic[ENV_TOPIC_MAX_LEN];
  char unique_id[THEO_IDENTITY_MAX_SLUG + 32];
  bool discovery_sent;
  bool availability_online;
  bool availability_dirty;
  bool state_dirty;
  bool has_value;
  float last_value;
} sensor_channel_state_t;

typedef struct {
  bool started;
  bool last_mqtt_ready;
  bool offline_logged;
  aht_t aht;
  bool aht_initialized;
  bmp280_handle_t bmp_handle;
  bool bmp_initialized;
  TaskHandle_t task;
  SemaphoreHandle_t snapshot_lock;
  env_sensor_snapshot_t snapshot;
  sensor_channel_state_t channels[SENSOR_CH_COUNT];
  const theo_identity_t *identity;
} env_sensors_state_t;

static env_sensors_state_t s_env;

static const sensor_channel_desc_t k_channel_descs[SENSOR_CH_COUNT] = {
    [SENSOR_CH_TEMP_BMP] = {.object_id = "temperature_bmp", .device_class = "temperature", .unit = "\xC2\xB0C", .source = SENSOR_SRC_BMP},
    [SENSOR_CH_TEMP_AHT] = {.object_id = "temperature_aht", .device_class = "temperature", .unit = "\xC2\xB0C", .source = SENSOR_SRC_AHT},
    [SENSOR_CH_HUMIDITY] = {.object_id = "relative_humidity", .device_class = "humidity", .unit = "%", .source = SENSOR_SRC_AHT},
    [SENSOR_CH_PRESSURE] = {.object_id = "air_pressure", .device_class = "pressure", .unit = "kPa", .source = SENSOR_SRC_BMP},
};

static esp_err_t build_topics(const theo_identity_t *identity);
static void env_sensor_task(void *arg);
static void sample_aht(void);
static void sample_bmp(void);
static void mark_channel_online(sensor_channel_t channel, bool online);
static void update_channel_value(sensor_channel_t channel, float value);
static void publish_discovery_if_needed(esp_mqtt_client_handle_t client);
static void publish_availability_if_needed(esp_mqtt_client_handle_t client);
static void publish_states_if_needed(esp_mqtt_client_handle_t client);
static void handle_mqtt_transition(bool now_ready);
static void reset_dirty_flags(void);

esp_err_t env_sensors_start(const theo_identity_t *identity)
{
  ESP_RETURN_ON_FALSE(identity != NULL, ESP_ERR_INVALID_ARG, TAG, "identity is required");
  if (s_env.started)
  {
    return ESP_OK;
  }

  ESP_RETURN_ON_ERROR(i2cdev_init(), TAG, "i2cdev init failed");

  memset(&s_env, 0, sizeof(s_env));
  s_env.identity = identity;
  s_env.snapshot_lock = xSemaphoreCreateMutex();
  ESP_RETURN_ON_FALSE(s_env.snapshot_lock != NULL, ESP_ERR_NO_MEM, TAG, "snapshot mutex alloc failed");

  for (size_t i = 0; i < SENSOR_CH_COUNT; ++i)
  {
    s_env.channels[i].desc = k_channel_descs[i];
  }

  s_env.aht.mode = AHT_MODE_NORMAL;
  s_env.aht.type = AHT_TYPE_AHT20;
  esp_err_t err = aht_init_desc(&s_env.aht, AHT_I2C_ADDRESS_GND, ENV_I2C_PORT,
                                CONFIG_THEO_I2C_ENV_SDA_GPIO, CONFIG_THEO_I2C_ENV_SCL_GPIO);
  if (err != ESP_OK)
  {
    ESP_LOGE(TAG, "AHT descriptor init failed: %s", esp_err_to_name(err));
    goto fail;
  }
  err = aht_init(&s_env.aht);
  if (err != ESP_OK)
  {
    ESP_LOGE(TAG, "AHT init failed: %s", esp_err_to_name(err));
    goto fail;
  }
  s_env.aht_initialized = true;

  i2c_master_bus_handle_t bus = NULL;
  err = i2c_master_get_bus_handle(ENV_I2C_PORT, &bus);
  if (err != ESP_OK)
  {
    ESP_LOGE(TAG, "Failed to fetch I2C bus handle: %s", esp_err_to_name(err));
    goto fail;
  }

  bmp280_config_t bmp_cfg = I2C_BMP280_CONFIG_DEFAULT;
  bmp_cfg.i2c_clock_speed = 400000;
  const uint16_t candidate_addrs[] = {I2C_BMP280_DEV_ADDR_HI, I2C_BMP280_DEV_ADDR_LO};
  bool bmp_found = false;
  for (size_t i = 0; i < sizeof(candidate_addrs) / sizeof(candidate_addrs[0]); ++i)
  {
    bmp_cfg.i2c_address = candidate_addrs[i];
    err = bmp280_init(bus, &bmp_cfg, &s_env.bmp_handle);
    if (err == ESP_OK)
    {
      ESP_LOGI(TAG, "BMP280 detected at 0x%02x", candidate_addrs[i]);
      bmp_found = true;
      break;
    }
  }
  if (!bmp_found)
  {
    ESP_LOGE(TAG, "BMP280 init failed: %s", esp_err_to_name(err));
    goto fail;
  }
  s_env.bmp_initialized = true;

  err = build_topics(identity);
  if (err != ESP_OK)
  {
    ESP_LOGE(TAG, "Topic build failed: %s", esp_err_to_name(err));
    goto fail;
  }

  BaseType_t task_ok = xTaskCreate(env_sensor_task, "env_sensors", ENV_TASK_STACK, NULL, ENV_TASK_PRIORITY, &s_env.task);
  if (task_ok != pdPASS)
  {
    ESP_LOGE(TAG, "Failed to create env sensor task");
    err = ESP_ERR_NO_MEM;
    goto fail;
  }

  s_env.started = true;
  ESP_LOGI(TAG, "Environmental sensors ready (poll=%ds base=%s)", CONFIG_THEO_SENSOR_POLL_SECONDS, identity->base_topic);
  return ESP_OK;

fail:
  if (s_env.task)
  {
    vTaskDelete(s_env.task);
    s_env.task = NULL;
  }
  if (s_env.bmp_initialized)
  {
    bmp280_delete(s_env.bmp_handle);
    s_env.bmp_handle = NULL;
    s_env.bmp_initialized = false;
  }
  if (s_env.aht_initialized)
  {
    aht_free_desc(&s_env.aht);
    s_env.aht_initialized = false;
  }
  if (s_env.snapshot_lock)
  {
    vSemaphoreDelete(s_env.snapshot_lock);
    s_env.snapshot_lock = NULL;
  }
  return err;
}

esp_err_t env_sensors_get_snapshot(env_sensor_snapshot_t *snapshot)
{
  ESP_RETURN_ON_FALSE(snapshot != NULL, ESP_ERR_INVALID_ARG, TAG, "snapshot arg required");
  if (s_env.snapshot_lock == NULL)
  {
    memset(snapshot, 0, sizeof(*snapshot));
    return ESP_ERR_INVALID_STATE;
  }
  if (xSemaphoreTake(s_env.snapshot_lock, pdMS_TO_TICKS(50)) != pdTRUE)
  {
    return ESP_ERR_TIMEOUT;
  }
  *snapshot = s_env.snapshot;
  xSemaphoreGive(s_env.snapshot_lock);
  return ESP_OK;
}

static esp_err_t build_topics(const theo_identity_t *identity)
{
  for (size_t i = 0; i < SENSOR_CH_COUNT; ++i)
  {
    sensor_channel_state_t *ch = &s_env.channels[i];
    int written = snprintf(ch->state_topic,
                           sizeof(ch->state_topic),
                           "%s/sensor/%s/%s/state",
                           identity->base_topic,
                           identity->object_prefix,
                           ch->desc.object_id);
    ESP_RETURN_ON_FALSE(written > 0 && written < (int)sizeof(ch->state_topic), ESP_ERR_INVALID_SIZE, TAG, "state topic overflow");

    written = snprintf(ch->availability_topic,
                       sizeof(ch->availability_topic),
                       "%s/sensor/%s/%s/availability",
                       identity->base_topic,
                       identity->object_prefix,
                       ch->desc.object_id);
    ESP_RETURN_ON_FALSE(written > 0 && written < (int)sizeof(ch->availability_topic), ESP_ERR_INVALID_SIZE, TAG, "avail topic overflow");

    written = snprintf(ch->discovery_topic,
                       sizeof(ch->discovery_topic),
                       "%s/sensor/%s/%s/config",
                       ENV_DISCOVERY_PREFIX,
                       identity->object_prefix,
                       ch->desc.object_id);
    ESP_RETURN_ON_FALSE(written > 0 && written < (int)sizeof(ch->discovery_topic), ESP_ERR_INVALID_SIZE, TAG, "discovery topic overflow");

    written = snprintf(ch->unique_id,
                       sizeof(ch->unique_id),
                       "%s_%s",
                       identity->device_identifier,
                       ch->desc.object_id);
    ESP_RETURN_ON_FALSE(written > 0 && written < (int)sizeof(ch->unique_id), ESP_ERR_INVALID_SIZE, TAG, "unique id overflow");

    ch->availability_online = false;
    ch->availability_dirty = true;
    ch->discovery_sent = false;
    ch->state_dirty = false;
    ch->has_value = false;
  }
  return ESP_OK;
}

static void env_sensor_task(void *arg)
{
  (void)arg;
  while (true)
  {
    sample_aht();
    sample_bmp();

    bool mqtt_ready = mqtt_manager_is_ready();
    esp_mqtt_client_handle_t client = mqtt_manager_get_client();
    if (!mqtt_ready || client == NULL)
    {
      if (!s_env.offline_logged)
      {
        ESP_LOGW(TAG, "Telemetry publish skipped (MQTT offline)");
        s_env.offline_logged = true;
      }
      handle_mqtt_transition(false);
    }
    else
    {
      handle_mqtt_transition(true);
      publish_discovery_if_needed(client);
      publish_availability_if_needed(client);
      publish_states_if_needed(client);
    }

    vTaskDelay(pdMS_TO_TICKS(CONFIG_THEO_SENSOR_POLL_SECONDS * 1000));
  }
}

static void sample_aht(void)
{
  if (!s_env.aht_initialized)
  {
    return;
  }
  float temperature_c = 0.0f;
  float humidity_percent = 0.0f;
  esp_err_t err = aht_get_data(&s_env.aht, &temperature_c, &humidity_percent);
  if (err != ESP_OK)
  {
    ESP_LOGW(TAG, "AHT read failed: %s", esp_err_to_name(err));
    mark_channel_online(SENSOR_CH_TEMP_AHT, false);
    mark_channel_online(SENSOR_CH_HUMIDITY, false);
    return;
  }
  int64_t now = esp_timer_get_time();
  if (s_env.snapshot_lock && xSemaphoreTake(s_env.snapshot_lock, pdMS_TO_TICKS(20)) == pdTRUE)
  {
    s_env.snapshot.temperature_aht_c = temperature_c;
    s_env.snapshot.humidity_percent = humidity_percent;
    s_env.snapshot.aht_timestamp_us = now;
    s_env.snapshot.aht_valid = true;
    xSemaphoreGive(s_env.snapshot_lock);
  }
  update_channel_value(SENSOR_CH_TEMP_AHT, temperature_c);
  update_channel_value(SENSOR_CH_HUMIDITY, humidity_percent);
  mark_channel_online(SENSOR_CH_TEMP_AHT, true);
  mark_channel_online(SENSOR_CH_HUMIDITY, true);
}

static void sample_bmp(void)
{
  if (!s_env.bmp_initialized)
  {
    return;
  }
  float temperature_c = 0.0f;
  float pressure_pa = 0.0f;
  esp_err_t err = bmp280_get_measurements(s_env.bmp_handle, &temperature_c, &pressure_pa);
  if (err != ESP_OK)
  {
    ESP_LOGW(TAG, "BMP280 read failed: %s", esp_err_to_name(err));
    mark_channel_online(SENSOR_CH_TEMP_BMP, false);
    mark_channel_online(SENSOR_CH_PRESSURE, false);
    return;
  }
  float pressure_kpa = pressure_pa / 1000.0f;
  int64_t now = esp_timer_get_time();
  if (s_env.snapshot_lock && xSemaphoreTake(s_env.snapshot_lock, pdMS_TO_TICKS(20)) == pdTRUE)
  {
    s_env.snapshot.temperature_bmp_c = temperature_c;
    s_env.snapshot.pressure_kpa = pressure_kpa;
    s_env.snapshot.bmp_timestamp_us = now;
    s_env.snapshot.bmp_valid = true;
    xSemaphoreGive(s_env.snapshot_lock);
  }
  update_channel_value(SENSOR_CH_TEMP_BMP, temperature_c);
  update_channel_value(SENSOR_CH_PRESSURE, pressure_kpa);
  mark_channel_online(SENSOR_CH_TEMP_BMP, true);
  mark_channel_online(SENSOR_CH_PRESSURE, true);
}

static void handle_mqtt_transition(bool now_ready)
{
  if (now_ready && !s_env.last_mqtt_ready)
  {
    s_env.offline_logged = false;
    reset_dirty_flags();
  }
  s_env.last_mqtt_ready = now_ready;
}

static void reset_dirty_flags(void)
{
  for (size_t i = 0; i < SENSOR_CH_COUNT; ++i)
  {
    sensor_channel_state_t *ch = &s_env.channels[i];
    ch->discovery_sent = false;
    if (ch->has_value)
    {
      ch->state_dirty = true;
    }
    ch->availability_dirty = true;
  }
}

static void publish_discovery_if_needed(esp_mqtt_client_handle_t client)
{
  char device_name[THEO_IDENTITY_MAX_FRIENDLY + 16];
  if (theo_identity_format_device_name(device_name, sizeof(device_name)) != ESP_OK)
  {
    return;
  }
  for (size_t i = 0; i < SENSOR_CH_COUNT; ++i)
  {
    sensor_channel_state_t *ch = &s_env.channels[i];
    if (ch->discovery_sent)
    {
      continue;
    }
    char payload[512];
    int written = snprintf(payload,
                           sizeof(payload),
                           "{\"name\":\"%s\",\"unique_id\":\"%s\",\"device_class\":\"%s\",\"state_class\":\"measurement\",\"unit_of_measurement\":\"%s\",\"state_topic\":\"%s\",\"availability_topic\":\"%s\",\"payload_available\":\"online\",\"payload_not_available\":\"offline\",\"device\":{\"name\":\"%s\",\"identifiers\":[\"%s\"],\"manufacturer\":\"%s\",\"model\":\"%s\"}}",
                           ch->desc.object_id,
                           ch->unique_id,
                           ch->desc.device_class,
                           ch->desc.unit,
                           ch->state_topic,
                           ch->availability_topic,
                           device_name,
                           s_env.identity->device_identifier,
                           ENV_MANUFACTURER,
                           ENV_MODEL);
    if (written <= 0 || written >= (int)sizeof(payload))
    {
      ESP_LOGE(TAG, "Discovery payload overflow for %s", ch->desc.object_id);
      continue;
    }
    int msg_id = esp_mqtt_client_publish(client, ch->discovery_topic, payload, 0, 0, 1);
    if (msg_id < 0)
    {
      ESP_LOGW(TAG, "Discovery publish failed for %s", ch->desc.object_id);
      continue;
    }
    ch->discovery_sent = true;
  }
}

static void publish_availability_if_needed(esp_mqtt_client_handle_t client)
{
  for (size_t i = 0; i < SENSOR_CH_COUNT; ++i)
  {
    sensor_channel_state_t *ch = &s_env.channels[i];
    if (!ch->availability_dirty)
    {
      continue;
    }
    const char *payload = ch->availability_online ? "online" : "offline";
    int msg_id = esp_mqtt_client_publish(client, ch->availability_topic, payload, 0, 0, 1);
    if (msg_id < 0)
    {
      ESP_LOGW(TAG, "Availability publish failed for %s", ch->desc.object_id);
      continue;
    }
    ch->availability_dirty = false;
  }
}

static void publish_states_if_needed(esp_mqtt_client_handle_t client)
{
  for (size_t i = 0; i < SENSOR_CH_COUNT; ++i)
  {
    sensor_channel_state_t *ch = &s_env.channels[i];
    if (!ch->state_dirty || !ch->has_value)
    {
      continue;
    }
    char payload[24];
    int written = snprintf(payload, sizeof(payload), "%.2f", ch->last_value);
    if (written <= 0 || written >= (int)sizeof(payload))
    {
      ESP_LOGW(TAG, "State payload overflow for %s", ch->desc.object_id);
      continue;
    }
    int msg_id = esp_mqtt_client_publish(client, ch->state_topic, payload, 0, 0, 1);
    if (msg_id < 0)
    {
      ESP_LOGW(TAG, "State publish failed for %s", ch->desc.object_id);
      continue;
    }
    ch->state_dirty = false;
  }
}

static void update_channel_value(sensor_channel_t channel, float value)
{
  if (channel >= SENSOR_CH_COUNT)
  {
    return;
  }
  sensor_channel_state_t *ch = &s_env.channels[channel];
  ch->last_value = value;
  ch->has_value = true;
  ch->state_dirty = true;
}

static void mark_channel_online(sensor_channel_t channel, bool online)
{
  if (channel >= SENSOR_CH_COUNT)
  {
    return;
  }
  sensor_channel_state_t *ch = &s_env.channels[channel];
  if (ch->availability_online != online)
  {
    ch->availability_online = online;
    ch->availability_dirty = true;
  }
}
