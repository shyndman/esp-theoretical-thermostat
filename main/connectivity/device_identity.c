#include "device_identity.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <esp_log.h>
#include "esp_attr.h"
#include "sdkconfig.h"

static const char *TAG = "device_identity";

static EXT_RAM_BSS_ATTR char s_device_slug[32];
static EXT_RAM_BSS_ATTR char s_device_friendly_name[64];
static EXT_RAM_BSS_ATTR char s_theo_base_topic[160];
static EXT_RAM_BSS_ATTR char s_theo_device_topic_root[160];
static bool s_initialized = false;

static void normalize_slug(const char *input, char *output, size_t output_len)
{
    if (input == NULL || output == NULL || output_len == 0) {
        if (output != NULL && output_len > 0) {
            output[0] = '\0';
        }
        return;
    }

    size_t out_idx = 0;
    bool prev_was_dash = true;

    for (size_t i = 0; input[i] != '\0' && out_idx < output_len - 1; ++i) {
        unsigned char raw = (unsigned char)input[i];
        char c = (char)tolower(raw);
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
            output[out_idx++] = c;
            prev_was_dash = false;
        } else if (!prev_was_dash) {
            output[out_idx++] = '-';
            prev_was_dash = true;
        }
    }

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

static void trim_topic_slashes(const char *input, char *output, size_t output_len)
{
    if (input == NULL || output == NULL || output_len == 0) {
        if (output != NULL && output_len > 0) {
            output[0] = '\0';
        }
        return;
    }

    const char *start = input;
    while (*start != '\0' && isspace((unsigned char)*start)) {
        ++start;
    }

    const char *end = input + strlen(input);
    while (end > start && isspace((unsigned char)end[-1])) {
        --end;
    }

    while (start < end && *start == '/') {
        ++start;
    }

    while (end > start && end[-1] == '/') {
        --end;
    }

    size_t len = (size_t)(end - start);
    if (len == 0 || len >= output_len) {
        output[0] = '\0';
        return;
    }

    memcpy(output, start, len);
    output[len] = '\0';
}

esp_err_t device_identity_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    // Slug
    normalize_slug(CONFIG_THEO_DEVICE_SLUG, s_device_slug, sizeof(s_device_slug));
    if (s_device_slug[0] == '\0') {
        strncpy(s_device_slug, "hallway", sizeof(s_device_slug) - 1);
        s_device_slug[sizeof(s_device_slug) - 1] = '\0';
    }
    ESP_LOGI(TAG, "Device slug: %s", s_device_slug);

    // Friendly name
    const char *cfg_friendly = CONFIG_THEO_DEVICE_FRIENDLY_NAME;
    if (cfg_friendly != NULL && cfg_friendly[0] != '\0') {
        strncpy(s_device_friendly_name, cfg_friendly, sizeof(s_device_friendly_name) - 1);
        s_device_friendly_name[sizeof(s_device_friendly_name) - 1] = '\0';
    } else {
        derive_friendly_name(s_device_slug, s_device_friendly_name, sizeof(s_device_friendly_name));
    }
    ESP_LOGI(TAG, "Device friendly name: %s", s_device_friendly_name);

    // Theo base topic
    trim_topic_slashes(CONFIG_THEO_THEOSTAT_BASE_TOPIC, s_theo_base_topic, sizeof(s_theo_base_topic));
    if (s_theo_base_topic[0] == '\0') {
        strncpy(s_theo_base_topic, "theostat", sizeof(s_theo_base_topic) - 1);
        s_theo_base_topic[sizeof(s_theo_base_topic) - 1] = '\0';
    }
    ESP_LOGI(TAG, "Theo base topic: %s", s_theo_base_topic);

    int written = snprintf(s_theo_device_topic_root,
                           sizeof(s_theo_device_topic_root),
                           "%s/%s",
                           s_theo_base_topic,
                           s_device_slug);
    if (written <= 0 || written >= (int)sizeof(s_theo_device_topic_root)) {
        ESP_LOGE(TAG, "Theo device topic root overflow");
        return ESP_ERR_INVALID_SIZE;
    }
    ESP_LOGI(TAG, "Theo device topic root: %s", s_theo_device_topic_root);

    s_initialized = true;
    return ESP_OK;
}

const char *device_identity_get_slug(void)
{
    return s_device_slug;
}

const char *device_identity_get_friendly_name(void)
{
    return s_device_friendly_name;
}

const char *device_identity_get_theo_base_topic(void)
{
    return s_theo_base_topic;
}

const char *device_identity_get_theo_device_topic_root(void)
{
    return s_theo_device_topic_root;
}
