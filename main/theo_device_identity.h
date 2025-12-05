#pragma once

#include "esp_err.h"

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define THEO_IDENTITY_MAX_SLUG 32
#define THEO_IDENTITY_MAX_FRIENDLY 32
#define THEO_IDENTITY_MAX_TOPIC 160

typedef struct {
  char slug[THEO_IDENTITY_MAX_SLUG + 1];
  char friendly_name[THEO_IDENTITY_MAX_FRIENDLY + 1];
  char base_topic[THEO_IDENTITY_MAX_TOPIC + 1];
  char object_prefix[THEO_IDENTITY_MAX_SLUG + 16];
  char device_identifier[THEO_IDENTITY_MAX_SLUG + 16];
} theo_identity_t;

esp_err_t theo_identity_init(void);
const theo_identity_t *theo_identity_get(void);
const char *theo_identity_slug(void);
const char *theo_identity_friendly_name(void);
const char *theo_identity_base_topic(void);
const char *theo_identity_object_prefix(void);
const char *theo_identity_device_identifier(void);
esp_err_t theo_identity_format_device_name(char *out, size_t out_len);

#ifdef __cplusplus
}
#endif
