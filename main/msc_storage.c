/*
 * MSC Storage for AtomS3 USB Joystick
 *
 * Manages FATfs storage partition with USB Mass Storage support.
 * Storage can be mounted either to app (VFS access) or USB (host access).
 */

#include "msc_storage.h"
#include "esp_log.h"
#include "esp_err.h"
#include "wear_levelling.h"
#include "tinyusb_msc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "stdio.h"
#include "string.h"
#include "ff.h"

static const char *TAG = "MSC_STORAGE";

#define STORAGE_MOUNT_POINT "/storage"
#define STORAGE_PARTITION_LABEL "storage"

// MSC storage handles
static wl_handle_t s_wl_handle = WL_INVALID_HANDLE;
static tinyusb_msc_storage_handle_t s_msc_storage_hdl = {0};
static bool s_msc_initialized = false;
static bool s_usb_mode_active = false;
// Set when the host issues a STOP UNIT / eject (macOS "Eject M5STACK"). The USB
// cable stays plugged in, so tud_mounted() remains true; the only signal is the
// mount-to-APP that tinyusb performs on eject (see tud_msc_start_stop_cb).
static volatile bool s_ejected = false;

// MSC event callback
static void msc_event_callback(tinyusb_msc_storage_handle_t handle,
                                tinyusb_msc_event_t *event,
                                void *arg)
{
    if (event == NULL) {
        return;
    }

    switch (event->id) {
        case TINYUSB_MSC_EVENT_MOUNT_START:
            ESP_LOGI(TAG, "MSC mount/unmount started");
            break;
        case TINYUSB_MSC_EVENT_MOUNT_COMPLETE:
            ESP_LOGI(TAG, "MSC mount/unmount complete (mount_point=%d)",
                     event->mount_point);
            // A mount-to-APP that arrives while USB MSC mode is already active
            // means the host issued STOP UNIT / eject (macOS "Eject"). tinyusb
            // has already remounted the storage back to the app; flag it so the
            // app can leave MSC and return to the menu.
            if (event->mount_point == TINYUSB_MSC_STORAGE_MOUNT_APP && s_usb_mode_active) {
                ESP_LOGI(TAG, "Host ejected the volume - flagging exit to menu");
                s_ejected = true;
                s_usb_mode_active = false;
            }
            break;
        case TINYUSB_MSC_EVENT_MOUNT_FAILED:
            ESP_LOGE(TAG, "MSC mount operation failed");
            break;
        case TINYUSB_MSC_EVENT_FORMAT_REQUIRED:
            ESP_LOGW(TAG, "MSC storage requires formatting");
            break;
        case TINYUSB_MSC_EVENT_FORMAT_FAILED:
            ESP_LOGE(TAG, "MSC format operation failed");
            break;
        default:
            break;
    }
}

esp_err_t msc_storage_init(void)
{
    if (s_msc_initialized) {
        ESP_LOGW(TAG, "MSC storage already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing MSC storage...");

    // Install MSC driver
    tinyusb_msc_driver_config_t driver_cfg = {
        .callback = msc_event_callback,
        .callback_arg = NULL,
    };

    esp_err_t ret = tinyusb_msc_install_driver(&driver_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install MSC driver: %s", esp_err_to_name(ret));
        return ret;
    }

    // Find storage partition
    const esp_partition_t *storage_partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA,
        ESP_PARTITION_SUBTYPE_DATA_FAT,
        STORAGE_PARTITION_LABEL
    );

    if (storage_partition == NULL) {
        ESP_LOGE(TAG, "Storage partition '%s' not found", STORAGE_PARTITION_LABEL);
        tinyusb_msc_uninstall_driver();
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Storage partition found: offset=0x%08" PRIx32 ", size=%" PRIu32 " KB",
             storage_partition->address, storage_partition->size / 1024);

    // Mount wear levelling
    ret = wl_mount(storage_partition, &s_wl_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount wear levelling: %s", esp_err_to_name(ret));
        tinyusb_msc_uninstall_driver();
        return ret;
    }

    // Create MSC storage on SPI flash
    tinyusb_msc_storage_config_t storage_cfg = {
        .medium = {
            .wl_handle = s_wl_handle,
        },
        .fat_fs = {
            .config = {
                .max_files = 4,
                .allocation_unit_size = 0,  // Auto-detect
            },
            .do_not_format = false,  // Format if filesystem doesn't exist
            .format_flags = FM_ANY,  // Any FAT format (FAT12/FAT16/FAT32)
            .base_path = STORAGE_MOUNT_POINT,
        },
        .mount_point = TINYUSB_MSC_STORAGE_MOUNT_APP,  // Initially mount to app
    };

    ret = tinyusb_msc_new_storage_spiflash(&storage_cfg, &s_msc_storage_hdl);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create MSC storage: %s", esp_err_to_name(ret));
        wl_unmount(s_wl_handle);
        tinyusb_msc_uninstall_driver();
        return ret;
    }

    // Wait for mount to complete
    vTaskDelay(pdMS_TO_TICKS(500));

    // Set volume label (max 11 chars for FAT32)
    ESP_LOGI(TAG, "Setting volume label to 'M5STACK'...");
    FRESULT res = f_setlabel("M5STACK");
    if (res == FR_OK) {
        ESP_LOGI(TAG, "Volume label set successfully");
    } else {
        ESP_LOGW(TAG, "Failed to set volume label: %d", res);
    }

    s_msc_initialized = true;
    s_usb_mode_active = false;

    ESP_LOGI(TAG, "MSC storage initialized successfully");
    ESP_LOGI(TAG, "Mount point: %s", STORAGE_MOUNT_POINT);

    // Create default config files if they don't exist
    msc_storage_create_default_configs();

    return ESP_OK;
}

esp_err_t msc_storage_start_usb_mode(void)
{
    if (!s_msc_initialized) {
        ESP_LOGE(TAG, "MSC storage not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (s_usb_mode_active) {
        ESP_LOGW(TAG, "USB mode already active");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Switching storage to USB MSC mode...");

    // Unmount from app
    esp_err_t ret = tinyusb_msc_set_storage_mount_point(
        s_msc_storage_hdl,
        TINYUSB_MSC_STORAGE_MOUNT_USB
    );

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to switch storage to USB: %s", esp_err_to_name(ret));
        return ret;
    }

    // Wait for remount
    vTaskDelay(pdMS_TO_TICKS(500));

    s_usb_mode_active = true;

    ESP_LOGI(TAG, "Storage now accessible via USB MSC");
    ESP_LOGI(TAG, "Safely eject device before disconnecting");

    return ESP_OK;
}

esp_err_t msc_storage_stop_usb_mode(void)
{
    if (!s_msc_initialized) {
        ESP_LOGE(TAG, "MSC storage not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (!s_usb_mode_active) {
        ESP_LOGW(TAG, "USB mode not active");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Switching storage back to app mode...");

    // Mount back to app
    esp_err_t ret = tinyusb_msc_set_storage_mount_point(
        s_msc_storage_hdl,
        TINYUSB_MSC_STORAGE_MOUNT_APP
    );

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to switch storage to app: %s", esp_err_to_name(ret));
        return ret;
    }

    // Wait for remount
    vTaskDelay(pdMS_TO_TICKS(500));

    s_usb_mode_active = false;

    ESP_LOGI(TAG, "Storage now accessible by app");

    return ESP_OK;
}

bool msc_storage_is_usb_active(void)
{
    return s_usb_mode_active;
}

// Returns true if the host ejected the volume (SCSI STOP UNIT) since the last
// check, and clears the flag so it fires exactly once until the next eject.
bool msc_storage_check_ejected(void)
{
    if (s_ejected) {
        s_ejected = false;
        return true;
    }
    return false;
}

const char* msc_storage_get_mount_point(void)
{
    return STORAGE_MOUNT_POINT;
}

esp_err_t msc_storage_create_default_configs(void)
{
    // Files are pre-populated in the FAT partition at build time
    // This function is kept for compatibility but no longer creates files
    ESP_LOGI(TAG, "Configuration files pre-loaded from storage/ directory");
    return ESP_OK;
}
