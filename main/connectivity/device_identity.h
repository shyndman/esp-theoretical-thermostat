#ifndef DEVICE_IDENTITY_H
#define DEVICE_IDENTITY_H

#include <esp_err.h>

/**
 * @brief Initialize the shared device identity.
 *
 * This function derives the device slug, friendly name, and Theo base topic
 * from Kconfig values and fallback rules. It must be called once early in boot.
 *
 * @return ESP_OK on success, or an error code on failure.
 */
esp_err_t device_identity_init(void);

/**
 * @brief Get the normalized device slug.
 * @return A read-only pointer to the slug string.
 */
const char *device_identity_get_slug(void);

/**
 * @brief Get the device friendly name.
 * @return A read-only pointer to the friendly name string.
 */
const char *device_identity_get_friendly_name(void);

/**
 * @brief Get the normalized Theo base topic.
 * @return A read-only pointer to the base topic string.
 */
const char *device_identity_get_theo_base_topic(void);

/**
 * @brief Get the canonical Theo device topic root.
 * @return A read-only pointer to the `<TheoBase>/<slug>` topic root.
 */
const char *device_identity_get_theo_device_topic_root(void);

#endif // DEVICE_IDENTITY_H
