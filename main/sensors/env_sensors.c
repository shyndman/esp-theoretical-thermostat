#include "sensors/env_sensors.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "esp_check.h"
#include "esp_log.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "mqtt_client.h"
#include "sdkconfig.h"

#include "ahtxx.h"
#include "bmp280.h"
#include "connectivity/mqtt_manager.h"

static const char *TAG = "env_sensors";

#define ENV_SENSORS_TASK_STACK    (5120)
#define ENV_SENSORS_TASK_PRIO     (4)
#define ENV_SENSORS_I2C_FREQ_HZ   (100000)
#define ENV_SENSORS_TOPIC_MAX_LEN (160)

typedef enum {
  SENSOR_ID_TEMPERATURE_BMP = 0,
  SENSOR_ID_TEMPERATURE_AHT,
  SENSOR_ID_RELATIVE_HUMIDITY,
  SENSOR_ID_AIR_PRESSURE,
  SENSOR_ID_COUNT,
} sensor_id_t;

typedef struct {
  const char *object_id;
  const char *name;
  const char *device_class;
  const char *unit;
  uint8_t consecutive_failures;
  bool online;
  bool discovery_published;
} sensor_meta_t;

static sensor_meta_t s_sensor_meta[SENSOR_ID_COUNT] = {
    [SENSOR_ID_TEMPERATURE_BMP] = {
        .object_id = "temperature_bmp",
        .name = "Temperature (BMP280)",
        .device_class = "temperature",
        .unit = "°C",
        .online = false,
        .discovery_published = false,
    },
    [SENSOR_ID_TEMPERATURE_AHT] = {
        .object_id = "temperature_aht",
        .name = "Temperature (AHT20)",
        .device_class = "temperature",
        .unit = "°C",
        .online = false,
        .discovery_published = false,
    },
    [SENSOR_ID_RELATIVE_HUMIDITY] = {
        .object_id = "relative_humidity",
        .name = "Humidity",
        .device_class = "humidity",
        .unit = "%",
        .online = false,
        .discovery_published = false,
    },
    [SENSOR_ID_AIR_PRESSURE] = {
        .object_id = "air_pressure",
        .name = "Pressure",
        .device_class = "atmospheric_pressure",
        .unit = "kPa",
        .online = false,
        .discovery_published = false,
    },
};

static i2c_master_bus_handle_t s_i2c_bus;
static ahtxx_handle_t s_ahtxx_handle;
static bmp280_handle_t s_bmp280_handle;
static TaskHandle_t s_task_handle;
static SemaphoreHandle_t s_readings_mutex;
static env_sensor_readings_t s_cached_readings;
static bool s_started;

static char s_device_slug[32];
static char s_device_friendly_name[64];
static char s_theo_base_topic[ENV_SENSORS_TOPIC_MAX_LEN];

static void env_sensors_task(void *arg);
static esp_err_t init_i2c_bus(void);
static esp_err_t init_ahtxx(void);
static esp_err_t init_bmp280(void);
static void normalize_slug(const char *input, char *output, size_t output_len);
static void derive_friendly_name(const char *slug, char *output, size_t output_len);
static void build_theo_base_topic(void);
static void publish_discovery_config(sensor_id_t sensor_id);
static void publish_availability(sensor_id_t sensor_id, bool online);
static void publish_state(sensor_id_t sensor_id, float value);
static void build_topic(char *buf, size_t buf_len, sensor_id_t sensor_id, const char *suffix);
static void handle_sensor_success(sensor_id_t sensor_id, float value);
static void handle_sensor_failure(sensor_id_t sensor_id);

esp_err_t env_sensors_start(void)
{
  if (s_started) {
    return ESP_OK;
  }

  ESP_LOGI(TAG, "Initializing environmental sensors");

  // Normalize device slug
  normalize_slug(CONFIG_THEO_DEVICE_SLUG, s_device_slug, sizeof(s_device_slug));
  if (s_device_slug[0] == '\0') {
    strncpy(s_device_slug, "hallway", sizeof(s_device_slug) - 1);
  }
  ESP_LOGI(TAG, "Device slug: %s", s_device_slug);

  // Derive friendly name
  const char *cfg_friendly = CONFIG_THEO_DEVICE_FRIENDLY_NAME;
  if (cfg_friendly != NULL && cfg_friendly[0] != '\0') {
    strncpy(s_device_friendly_name, cfg_friendly, sizeof(s_device_friendly_name) - 1);
  } else {
    derive_friendly_name(s_device_slug, s_device_friendly_name, sizeof(s_device_friendly_name));
  }
  ESP_LOGI(TAG, "Device friendly name: %s", s_device_friendly_name);

  // Build Theo base topic
  build_theo_base_topic();
  ESP_LOGI(TAG, "Theo base topic: %s", s_theo_base_topic);

  // Create readings mutex
  s_readings_mutex = xSemaphoreCreateMutex();
  ESP_RETURN_ON_FALSE(s_readings_mutex != NULL, ESP_ERR_NO_MEM, TAG, "Failed to create readings mutex");

  // Initialize I2C bus
  esp_err_t err = init_i2c_bus();
  if (err != ESP_OK) {
    vSemaphoreDelete(s_readings_mutex);
    s_readings_mutex = NULL;
    return err;
  }

  // Initialize AHT20
  err = init_ahtxx();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "AHT20 init failed: %s", esp_err_to_name(err));
    i2c_del_master_bus(s_i2c_bus);
    s_i2c_bus = NULL;
    vSemaphoreDelete(s_readings_mutex);
    s_readings_mutex = NULL;
    return err;
  }

  // Initialize BMP280
  err = init_bmp280();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "BMP280 init failed: %s", esp_err_to_name(err));
    ahtxx_delete(s_ahtxx_handle);
    i2c_del_master_bus(s_i2c_bus);
    s_i2c_bus = NULL;
    vSemaphoreDelete(s_readings_mutex);
    s_readings_mutex = NULL;
    return err;
  }

  // Create sampling task
  BaseType_t task_ok = xTaskCreate(
      env_sensors_task,
      "env_sens",
      ENV_SENSORS_TASK_STACK,
      NULL,
      ENV_SENSORS_TASK_PRIO,
      &s_task_handle);
  if (task_ok != pdPASS) {
    ESP_LOGE(TAG, "Failed to create sensor task");
    bmp280_delete(s_bmp280_handle);
    s_bmp280_handle = NULL;
    ahtxx_delete(s_ahtxx_handle);
    i2c_del_master_bus(s_i2c_bus);
    s_i2c_bus = NULL;
    vSemaphoreDelete(s_readings_mutex);
    s_readings_mutex = NULL;
    return ESP_ERR_NO_MEM;
  }

  s_started = true;
  ESP_LOGI(TAG, "Environmental sensors started (poll interval: %d s)", CONFIG_THEO_SENSOR_POLL_SECONDS);
  return ESP_OK;
}

esp_err_t env_sensors_get_readings(env_sensor_readings_t *readings)
{
  if (readings == NULL) {
    return ESP_ERR_INVALID_ARG;
  }
  if (s_readings_mutex == NULL) {
    return ESP_ERR_INVALID_STATE;
  }

  if (xSemaphoreTake(s_readings_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
    return ESP_ERR_TIMEOUT;
  }

  memcpy(readings, &s_cached_readings, sizeof(env_sensor_readings_t));
  xSemaphoreGive(s_readings_mutex);

  bool has_any = readings->temperature_aht_valid ||
                 readings->temperature_bmp_valid ||
                 readings->relative_humidity_valid ||
                 readings->air_pressure_valid;
  return has_any ? ESP_OK : ESP_ERR_NOT_FOUND;
}

bool env_sensors_all_online(void)
{
  for (int i = 0; i < SENSOR_ID_COUNT; ++i) {
    if (!s_sensor_meta[i].online) {
      return false;
    }
  }
  return true;
}

const char *env_sensors_get_theo_base_topic(void)
{
  return s_theo_base_topic;
}

const char *env_sensors_get_device_slug(void)
{
  return s_device_slug;
}

static esp_err_t init_i2c_bus(void)
{
  i2c_master_bus_config_t bus_cfg = {
      .clk_source = I2C_CLK_SRC_DEFAULT,
      .i2c_port = I2C_NUM_0,
      .scl_io_num = CONFIG_THEO_I2C_ENV_SCL_GPIO,
      .sda_io_num = CONFIG_THEO_I2C_ENV_SDA_GPIO,
      .glitch_ignore_cnt = 7,
      .flags.enable_internal_pullup = false,  // External pull-ups installed
  };

  esp_err_t err = i2c_new_master_bus(&bus_cfg, &s_i2c_bus);
  ESP_RETURN_ON_ERROR(err, TAG, "Failed to create I2C master bus");
  ESP_LOGI(TAG, "I2C bus created (SDA=%d, SCL=%d, freq=%d Hz)",
           CONFIG_THEO_I2C_ENV_SDA_GPIO, CONFIG_THEO_I2C_ENV_SCL_GPIO, ENV_SENSORS_I2C_FREQ_HZ);
  return ESP_OK;
}

static esp_err_t init_ahtxx(void)
{
  ahtxx_config_t aht_cfg = I2C_AHT20_CONFIG_DEFAULT;

  esp_err_t err = ahtxx_init(s_i2c_bus, &aht_cfg, &s_ahtxx_handle);
  ESP_RETURN_ON_ERROR(err, TAG, "Failed to initialize AHT20");
  ESP_LOGI(TAG, "AHT20 initialized");
  return ESP_OK;
}

static esp_err_t init_bmp280(void)
{
  bmp280_config_t bmp_cfg = I2C_BMP280_CONFIG_DEFAULT;

  esp_err_t err = bmp280_init(s_i2c_bus, &bmp_cfg, &s_bmp280_handle);
  ESP_RETURN_ON_ERROR(err, TAG, "Failed to initialize BMP280");
  ESP_LOGI(TAG, "BMP280 initialized");
  return ESP_OK;
}

static void normalize_slug(const char *input, char *output, size_t output_len)
{
  if (input == NULL || output == NULL || output_len == 0) {
    if (output != NULL && output_len > 0) {
      output[0] = '\0';
    }
    return;
  }

  size_t out_idx = 0;
  bool prev_was_dash = true;  // Prevent leading dashes

  for (size_t i = 0; input[i] != '\0' && out_idx < output_len - 1; ++i) {
    char c = input[i];
    if (c >= 'A' && c <= 'Z') {
      c = c - 'A' + 'a';  // Lowercase
    }
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
      output[out_idx++] = c;
      prev_was_dash = false;
    } else if ((c == '-' || c == '_' || c == ' ') && !prev_was_dash) {
      output[out_idx++] = '-';
      prev_was_dash = true;
    }
  }

  // Remove trailing dash
  while (out_idx > 0 && output[out_idx - 1] == '-') {
    --out_idx;
  }

  output[out_idx] = '\0';
}

static void derive_friendly_name(const char *slug, char *output, size_t output_len)
{
  if (slug == NULL || output == NULL || output_len == 0) {
    if (output != NULL && output_len > 0) {
      output[0] = '\0';
    }
    return;
  }

  size_t out_idx = 0;
  bool capitalize_next = true;

  for (size_t i = 0; slug[i] != '\0' && out_idx < output_len - 1; ++i) {
    char c = slug[i];
    if (c == '-') {
      if (out_idx < output_len - 1) {
        output[out_idx++] = ' ';
      }
      capitalize_next = true;
    } else if (capitalize_next && c >= 'a' && c <= 'z') {
      output[out_idx++] = c - 'a' + 'A';
      capitalize_next = false;
    } else {
      output[out_idx++] = c;
      capitalize_next = false;
    }
  }

  output[out_idx] = '\0';
}

static void build_theo_base_topic(void)
{
  const char *cfg_topic = CONFIG_THEO_THEOSTAT_BASE_TOPIC;

  if (cfg_topic != NULL && cfg_topic[0] != '\0') {
    // Use configured topic, stripping leading/trailing slashes
    const char *start = cfg_topic;
    while (*start == '/') {
      ++start;
    }
    size_t len = strlen(start);
    while (len > 0 && start[len - 1] == '/') {
      --len;
    }
    if (len > 0 && len < sizeof(s_theo_base_topic)) {
      memcpy(s_theo_base_topic, start, len);
      s_theo_base_topic[len] = '\0';
      return;
    }
  }

  // Auto-derive base topic
  snprintf(s_theo_base_topic, sizeof(s_theo_base_topic), "theostat");
}

static void build_topic(char *buf, size_t buf_len, sensor_id_t sensor_id, const char *suffix)
{
  int written = snprintf(buf, buf_len, "%s/sensor/%s/%s/%s",
                         s_theo_base_topic,
                         s_device_slug,
                         s_sensor_meta[sensor_id].object_id,
                         suffix);
  if (written < 0 || (size_t)written >= buf_len) {
    ESP_LOGW(TAG, "Topic truncated for %s/%s", s_sensor_meta[sensor_id].object_id, suffix);
  }
}

static void publish_discovery_config(sensor_id_t sensor_id)
{
  if (!mqtt_manager_is_ready()) {
    return;
  }

  esp_mqtt_client_handle_t client = mqtt_manager_get_client();
  if (client == NULL) {
    return;
  }

  sensor_meta_t *meta = &s_sensor_meta[sensor_id];

  // Build discovery topic: homeassistant/sensor/<slug>/<object_id>/config
  char discovery_topic[ENV_SENSORS_TOPIC_MAX_LEN];
  snprintf(discovery_topic, sizeof(discovery_topic),
           "homeassistant/sensor/%s/%s/config",
           s_device_slug, meta->object_id);

  // Build state and availability topics
  char state_topic[ENV_SENSORS_TOPIC_MAX_LEN];
  char avail_topic[ENV_SENSORS_TOPIC_MAX_LEN];
  build_topic(state_topic, sizeof(state_topic), sensor_id, "state");
  build_topic(avail_topic, sizeof(avail_topic), sensor_id, "availability");

  // Build unique ID
  char unique_id[64];
  snprintf(unique_id, sizeof(unique_id), "theostat_%s_%s", s_device_slug, meta->object_id);

  // Build device name
  char device_name[80];
  snprintf(device_name, sizeof(device_name), "%s Theostat", s_device_friendly_name);

  // Build device identifier
  char device_id[48];
  snprintf(device_id, sizeof(device_id), "theostat_%s", s_device_slug);

  // Build discovery payload
  char payload[768];
  int written = snprintf(payload, sizeof(payload),
      "{"
      "\"name\":\"%s\","
      "\"device_class\":\"%s\","
      "\"state_class\":\"measurement\","
      "\"unit_of_measurement\":\"%s\","
      "\"unique_id\":\"%s\","
      "\"state_topic\":\"%s\","
      "\"availability_topic\":\"%s\","
      "\"payload_available\":\"online\","
      "\"payload_not_available\":\"offline\","
      "\"device\":{"
        "\"name\":\"%s\","
        "\"identifiers\":[\"%s\"],"
        "\"manufacturer\":\"Theo\","
        "\"model\":\"Theostat v1\""
      "}"
      "}",
      meta->name,
      meta->device_class,
      meta->unit,
      unique_id,
      state_topic,
      avail_topic,
      device_name,
      device_id);

  if (written <= 0 || written >= (int)sizeof(payload)) {
    ESP_LOGE(TAG, "Discovery payload overflow for %s", meta->object_id);
    return;
  }

  int msg_id = esp_mqtt_client_publish(client, discovery_topic, payload, 0, 0, 1);  // QoS0, retained
  if (msg_id < 0) {
    ESP_LOGE(TAG, "Failed to publish discovery for %s", meta->object_id);
  } else {
    ESP_LOGI(TAG, "Published discovery config for %s (msg_id=%d)", meta->object_id, msg_id);
    meta->discovery_published = true;
  }
}

static void publish_availability(sensor_id_t sensor_id, bool online)
{
  if (!mqtt_manager_is_ready()) {
    return;
  }

  esp_mqtt_client_handle_t client = mqtt_manager_get_client();
  if (client == NULL) {
    return;
  }

  char topic[ENV_SENSORS_TOPIC_MAX_LEN];
  build_topic(topic, sizeof(topic), sensor_id, "availability");

  const char *payload = online ? "online" : "offline";
  int msg_id = esp_mqtt_client_publish(client, topic, payload, 0, 0, 1);  // QoS0, retained
  if (msg_id < 0) {
    ESP_LOGW(TAG, "Failed to publish availability for %s", s_sensor_meta[sensor_id].object_id);
  } else {
    ESP_LOGI(TAG, "Published %s availability: %s", s_sensor_meta[sensor_id].object_id, payload);
  }
}

static void publish_state(sensor_id_t sensor_id, float value)
{
  if (!mqtt_manager_is_ready()) {
    ESP_LOGW(TAG, "MQTT not ready, skipping state publish for %s", s_sensor_meta[sensor_id].object_id);
    return;
  }

  esp_mqtt_client_handle_t client = mqtt_manager_get_client();
  if (client == NULL) {
    return;
  }

  char topic[ENV_SENSORS_TOPIC_MAX_LEN];
  build_topic(topic, sizeof(topic), sensor_id, "state");

  char payload[16];
  snprintf(payload, sizeof(payload), "%.2f", value);

  int msg_id = esp_mqtt_client_publish(client, topic, payload, 0, 0, 1);  // QoS0, retained
  if (msg_id < 0) {
    ESP_LOGW(TAG, "Failed to publish state for %s", s_sensor_meta[sensor_id].object_id);
  }
}

static void handle_sensor_success(sensor_id_t sensor_id, float value)
{
  sensor_meta_t *meta = &s_sensor_meta[sensor_id];
  bool was_offline = !meta->online;

  meta->consecutive_failures = 0;

  // Publish discovery config if not yet done
  if (!meta->discovery_published) {
    publish_discovery_config(sensor_id);
  }

  // Transition to online if needed
  if (was_offline) {
    meta->online = true;
    publish_availability(sensor_id, true);
  }

  // Publish state
  publish_state(sensor_id, value);
}

static void handle_sensor_failure(sensor_id_t sensor_id)
{
  sensor_meta_t *meta = &s_sensor_meta[sensor_id];
  meta->consecutive_failures++;

  ESP_LOGW(TAG, "%s read failed (%d/%d)",
           meta->object_id,
           meta->consecutive_failures,
           CONFIG_THEO_SENSOR_FAIL_THRESHOLD);

  if (meta->online && meta->consecutive_failures >= CONFIG_THEO_SENSOR_FAIL_THRESHOLD) {
    meta->online = false;
    publish_availability(sensor_id, false);
    ESP_LOGW(TAG, "%s marked offline after %d consecutive failures",
             meta->object_id, CONFIG_THEO_SENSOR_FAIL_THRESHOLD);
  }
}

static void env_sensors_task(void *arg)
{
  (void)arg;

  const TickType_t poll_interval = pdMS_TO_TICKS(CONFIG_THEO_SENSOR_POLL_SECONDS * 1000);

  ESP_LOGI(TAG, "Sensor task started, poll interval: %d ms", CONFIG_THEO_SENSOR_POLL_SECONDS * 1000);

  while (true) {
    float aht_temp = 0.0f;
    float aht_hum = 0.0f;
    float bmp_temp = 0.0f;
    float bmp_pressure_pa = 0.0f;

    // Read AHT20
    esp_err_t aht_err = ahtxx_get_measurement(s_ahtxx_handle, &aht_temp, &aht_hum);
    if (aht_err == ESP_OK && isfinite(aht_temp) && isfinite(aht_hum)) {
      ESP_LOGI(TAG, "AHT20: temp=%.2f°C, humidity=%.2f%%", aht_temp, aht_hum);

      if (xSemaphoreTake(s_readings_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        s_cached_readings.temperature_aht_c = aht_temp;
        s_cached_readings.temperature_aht_valid = true;
        s_cached_readings.relative_humidity = aht_hum;
        s_cached_readings.relative_humidity_valid = true;
        xSemaphoreGive(s_readings_mutex);
      }

      handle_sensor_success(SENSOR_ID_TEMPERATURE_AHT, aht_temp);
      handle_sensor_success(SENSOR_ID_RELATIVE_HUMIDITY, aht_hum);
    } else {
      ESP_LOGW(TAG, "AHT20 read failed: %s", esp_err_to_name(aht_err));
      handle_sensor_failure(SENSOR_ID_TEMPERATURE_AHT);
      handle_sensor_failure(SENSOR_ID_RELATIVE_HUMIDITY);
    }

    // Read BMP280
    esp_err_t bmp_err = bmp280_get_measurements(s_bmp280_handle, &bmp_temp, &bmp_pressure_pa);
    if (bmp_err == ESP_OK && isfinite(bmp_temp) && isfinite(bmp_pressure_pa)) {
      float pressure_kpa = bmp_pressure_pa / 1000.0f;
      ESP_LOGI(TAG, "BMP280: temp=%.2f°C, pressure=%.2f kPa", bmp_temp, pressure_kpa);

      if (xSemaphoreTake(s_readings_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        s_cached_readings.temperature_bmp_c = bmp_temp;
        s_cached_readings.temperature_bmp_valid = true;
        s_cached_readings.air_pressure_kpa = pressure_kpa;
        s_cached_readings.air_pressure_valid = true;
        xSemaphoreGive(s_readings_mutex);
      }

      handle_sensor_success(SENSOR_ID_TEMPERATURE_BMP, bmp_temp);
      handle_sensor_success(SENSOR_ID_AIR_PRESSURE, pressure_kpa);
    } else {
      ESP_LOGW(TAG, "BMP280 read failed: %s", esp_err_to_name(bmp_err));
      handle_sensor_failure(SENSOR_ID_TEMPERATURE_BMP);
      handle_sensor_failure(SENSOR_ID_AIR_PRESSURE);
    }

    vTaskDelay(poll_interval);
  }
}
