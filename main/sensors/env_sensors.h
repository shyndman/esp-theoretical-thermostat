#pragma once

#include "esp_err.h"

#include <stdbool.h>
#include <stdint.h>

#include "theo_device_identity.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  float temperature_aht_c;
  float humidity_percent;
  float temperature_bmp_c;
  float pressure_kpa;
  int64_t aht_timestamp_us;
  int64_t bmp_timestamp_us;
  bool aht_valid;
  bool bmp_valid;
} env_sensor_snapshot_t;

esp_err_t env_sensors_start(const theo_identity_t *identity);
esp_err_t env_sensors_get_snapshot(env_sensor_snapshot_t *snapshot);

#ifdef __cplusplus
}
#endif
