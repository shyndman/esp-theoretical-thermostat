#include "theo_device_identity.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "sdkconfig.h"

static const char *TAG = "theo_ident";
static const char *k_default_slug = "hallway";
static const char *k_default_base_prefix = "theostat";

static theo_identity_t s_identity;
static bool s_identity_ready;

static void trim_copy(const char *src, char *dst, size_t dst_len)
{
  if (!dst || dst_len == 0)
  {
    return;
  }
  dst[0] = '\0';
  if (!src)
  {
    return;
  }
  while (*src && isspace((unsigned char)*src))
  {
    ++src;
  }
  size_t len = strlen(src);
  while (len > 0 && isspace((unsigned char)src[len - 1]))
  {
    --len;
  }
  size_t copy_len = len < (dst_len - 1) ? len : (dst_len - 1);
  if (copy_len > 0)
  {
    memcpy(dst, src, copy_len);
  }
  dst[copy_len] = '\0';
}

static void sanitize_slug(char *out, size_t out_len)
{
  char trimmed[THEO_IDENTITY_MAX_SLUG * 2];
  trim_copy(CONFIG_THEO_DEVICE_SLUG, trimmed, sizeof(trimmed));
  const char *source = trimmed[0] == '\0' ? k_default_slug : trimmed;
  size_t out_idx = 0;
  bool prev_dash = true;
  for (const char *p = source; *p && out_idx < (out_len - 1); ++p)
  {
    char c = (char)tolower((unsigned char)*p);
    bool valid = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
    if (valid)
    {
      out[out_idx++] = c;
      prev_dash = false;
    }
    else
    {
      if (!prev_dash)
      {
        out[out_idx++] = '-';
        prev_dash = true;
      }
    }
    if (out_idx >= (out_len - 1))
    {
      break;
    }
  }
  if (out_idx == 0)
  {
    strlcpy(out, k_default_slug, out_len);
    return;
  }
  if (prev_dash && out_idx > 0)
  {
    --out_idx;
  }
  out[out_idx] = '\0';
}

static void slug_to_title(const char *slug, char *out, size_t out_len)
{
  size_t out_idx = 0;
  bool capitalize = true;
  for (const char *p = slug; *p && out_idx < (out_len - 1); ++p)
  {
    if (*p == '-')
    {
      if (out_idx < (out_len - 1))
      {
        out[out_idx++] = ' ';
      }
      capitalize = true;
      continue;
    }
    char c = *p;
    if (capitalize && c >= 'a' && c <= 'z')
    {
      c = (char)toupper((unsigned char)c);
    }
    else if (capitalize && c >= '0' && c <= '9')
    {
      c = c;
    }
    out[out_idx++] = c;
    capitalize = false;
  }
  while (out_idx > 0 && out[out_idx - 1] == ' ')
  {
    --out_idx;
  }
  out[out_idx] = '\0';
  if (out_idx == 0)
  {
    strlcpy(out, "Device", out_len);
  }
}

static bool is_visible_ascii(char c)
{
  return c >= 0x20 && c <= 0x7E && c != '"' && c != '\\';
}

static void sanitize_friendly_name(const char *candidate, char *out, size_t out_len, const char *slug)
{
  if (!candidate || candidate[0] == '\0')
  {
    slug_to_title(slug, out, out_len);
    return;
  }
  size_t len = strlen(candidate);
  if (len == 0 || len > THEO_IDENTITY_MAX_FRIENDLY)
  {
    slug_to_title(slug, out, out_len);
    return;
  }
  for (size_t i = 0; i < len; ++i)
  {
    if (!is_visible_ascii(candidate[i]))
    {
      slug_to_title(slug, out, out_len);
      return;
    }
  }
  strlcpy(out, candidate, out_len);
}

static void sanitize_base_topic(const char *candidate, char *out, size_t out_len, const char *slug)
{
  if (!candidate || candidate[0] == '\0')
  {
    snprintf(out, out_len, "%s/%s", k_default_base_prefix, slug);
    return;
  }
  size_t out_idx = 0;
  bool prev_sep = false;
  const char *p = candidate;
  while (*p && out_idx < (out_len - 1))
  {
    char c = *p++;
    if (isspace((unsigned char)c))
    {
      continue;
    }
    if (c == '\\')
    {
      c = '/';
    }
    if (c == '/')
    {
      if (out_idx == 0 || prev_sep)
      {
        continue;
      }
      out[out_idx++] = '/';
      prev_sep = true;
      continue;
    }
    out[out_idx++] = c;
    prev_sep = false;
  }
  while (out_idx > 0 && out[out_idx - 1] == '/')
  {
    --out_idx;
  }
  if (out_idx == 0)
  {
    snprintf(out, out_len, "%s/%s", k_default_base_prefix, slug);
  }
  else
  {
    out[out_idx] = '\0';
  }
}

static esp_err_t compute_identity(void)
{
  sanitize_slug(s_identity.slug, sizeof(s_identity.slug));

  char friendly_candidate[THEO_IDENTITY_MAX_FRIENDLY + 1];
  trim_copy(CONFIG_THEO_DEVICE_FRIENDLY_NAME, friendly_candidate, sizeof(friendly_candidate));
  sanitize_friendly_name(friendly_candidate, s_identity.friendly_name, sizeof(s_identity.friendly_name), s_identity.slug);

  char base_candidate[THEO_IDENTITY_MAX_TOPIC * 2];
  trim_copy(CONFIG_THEO_THEOSTAT_BASE_TOPIC, base_candidate, sizeof(base_candidate));
  sanitize_base_topic(base_candidate, s_identity.base_topic, sizeof(s_identity.base_topic), s_identity.slug);

  snprintf(s_identity.object_prefix, sizeof(s_identity.object_prefix), "%s-theostat", s_identity.slug);
  snprintf(s_identity.device_identifier, sizeof(s_identity.device_identifier), "%s_%s", k_default_base_prefix, s_identity.slug);

  return ESP_OK;
}

esp_err_t theo_identity_init(void)
{
  if (s_identity_ready)
  {
    return ESP_OK;
  }
  ESP_RETURN_ON_ERROR(compute_identity(), TAG, "identity compute failed");
  s_identity_ready = true;
  ESP_LOGI(TAG, "slug=%s friendly=\"%s\" theo_base=%s", s_identity.slug, s_identity.friendly_name, s_identity.base_topic);
  return ESP_OK;
}

static const theo_identity_t *ensure_identity(void)
{
  if (!s_identity_ready)
  {
    if (theo_identity_init() != ESP_OK)
    {
      return NULL;
    }
  }
  return &s_identity;
}

const theo_identity_t *theo_identity_get(void)
{
  return ensure_identity();
}

const char *theo_identity_slug(void)
{
  const theo_identity_t *id = ensure_identity();
  return id ? id->slug : NULL;
}

const char *theo_identity_friendly_name(void)
{
  const theo_identity_t *id = ensure_identity();
  return id ? id->friendly_name : NULL;
}

const char *theo_identity_base_topic(void)
{
  const theo_identity_t *id = ensure_identity();
  return id ? id->base_topic : NULL;
}

const char *theo_identity_object_prefix(void)
{
  const theo_identity_t *id = ensure_identity();
  return id ? id->object_prefix : NULL;
}

const char *theo_identity_device_identifier(void)
{
  const theo_identity_t *id = ensure_identity();
  return id ? id->device_identifier : NULL;
}

esp_err_t theo_identity_format_device_name(char *out, size_t out_len)
{
  ESP_RETURN_ON_FALSE(out != NULL && out_len > 0, ESP_ERR_INVALID_ARG, TAG, "invalid buffer");
  const theo_identity_t *id = ensure_identity();
  ESP_RETURN_ON_FALSE(id != NULL, ESP_ERR_INVALID_STATE, TAG, "identity not ready");
  int written = snprintf(out, out_len, "%s Theostat", id->friendly_name);
  ESP_RETURN_ON_FALSE(written > 0 && written < (int)out_len, ESP_ERR_INVALID_SIZE, TAG, "device name overflow");
  return ESP_OK;
}
