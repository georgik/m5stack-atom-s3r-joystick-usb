#ifndef MSC_STORAGE_H
#define MSC_STORAGE_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize MSC storage with FATfs filesystem
 *
 * Mounts the storage partition for app access. Must be called before
 * using other MSC storage functions.
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t msc_storage_init(void);

/**
 * @brief Start USB Mass Storage Mode
 *
 * Unmounts storage from app and makes it available via USB to host.
 * Host can read/write files (configs, profiles, etc).
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t msc_storage_start_usb_mode(void);

/**
 * @brief Stop USB Mass Storage Mode
 *
 * Remounts storage to app for file access.
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t msc_storage_stop_usb_mode(void);

/**
 * @brief Check if USB MSC mode is active
 *
 * @return true if storage is mounted to USB, false if mounted to app
 */
bool msc_storage_is_usb_active(void);

/**
 * @brief Check and clear the host-ejected flag.
 *
 * Returns true once (until the next eject) after the host issues a STOP UNIT /
 * eject command (e.g. macOS "Eject M5STACK"), and clears the flag so it fires
 * exactly once per eject.
 *
 * @return true if the volume was ejected since the last call, false otherwise
 */
bool msc_storage_check_ejected(void);

/**
 * @brief Create default configuration files
 *
 * Creates profiles.ini and system.ini with default values if they
 * don't exist.
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t msc_storage_create_default_configs(void);

/**
 * @brief Get storage mount point path
 *
 * @return Path to storage mount point (e.g., "/storage")
 */
const char* msc_storage_get_mount_point(void);

#ifdef __cplusplus
}
#endif

#endif // MSC_STORAGE_H
