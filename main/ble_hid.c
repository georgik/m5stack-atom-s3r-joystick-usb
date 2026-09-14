/*
 * BLE HID Implementation for M5Stack Atom Joystick USB
 * Based on working ESP32-S3 BLE Keyboard implementation
 * Uses NimBLE stack instead of Bluedroid
 */

#include "ble_hid.h"
#include "esp_log.h"
#include "esp_hidd.h"
#include "esp_bt.h"
#include "nimble/ble.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "nvs_flash.h"

static const char *TAG = BLE_HID_TAG;

// HID Service UUID (0x1812)
static const ble_uuid16_t hid_uuid = BLE_UUID16_INIT(0x1812);

// Gamepad HID report descriptor (16 buttons, 4 axes)
static const uint8_t gamepad_report_map[] = {
    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x05,        // Usage (Gamepad)
    0xA1, 0x01,        // Collection (Application)
    0x85, 0x03,        // Report ID (3)
    0xA1, 0x00,        // Collection (Physical)
    0x05, 0x09,        // Usage Page (Buttons)
    0x19, 0x01,        // Usage Minimum (01)
    0x29, 0x10,        // Usage Maximum (16)
    0x15, 0x00,        // Logical Minimum (0)
    0x25, 0x01,        // Logical Maximum (1)
    0x95, 0x10,        // Report Count (16)
    0x75, 0x01,        // Report Size (1)
    0x81, 0x02,        // Input (Data, Variable, Absolute)
    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x30,        // Usage (X)
    0x09, 0x31,        // Usage (Y)
    0x09, 0x33,        // Usage (Rx)
    0x09, 0x34,        // Usage (Ry)
    0x15, 0x81,        // Logical Minimum (-127)
    0x25, 0x7F,        // Logical Maximum (127)
    0x95, 0x04,        // Report Count (4)
    0x75, 0x08,        // Report Size (8)
    0x81, 0x02,        // Input (Data, Variable, Absolute)
    0xC0,              // End Collection
    0xC0,              // End Collection
};

// Report maps for BLE HID device
static esp_hid_raw_report_map_t ble_report_maps[] = {
    {
        .data = gamepad_report_map,
        .len = sizeof(gamepad_report_map)
    }
};

// BLE HID device configuration
static esp_hid_device_config_t ble_hid_config = {
    .vendor_id = 0xE502,
    .product_id = 0xBBAB,
    .version = 0x0100,
    .device_name = BLE_HID_DEVICE_NAME,
    .manufacturer_name = "M5Stack",
    .serial_number = "1234567890",
    .report_maps = ble_report_maps,
    .report_maps_len = 1
};

// BLE HID device handle and state
static esp_hidd_dev_t *s_ble_hid_dev = NULL;
static bool s_ble_connected = false;

// Set HID advertising data
static void set_hid_adv_data(void)
{
    struct ble_hs_adv_fields adv_fields = {
        .uuids16 = &hid_uuid,
        .num_uuids16 = 1,
        .uuids16_is_complete = 1,
        .appearance = 0x03C4,  // Gamepad appearance
        .appearance_is_present = 1,
        .name = (uint8_t *)BLE_HID_DEVICE_NAME,
        .name_len = sizeof(BLE_HID_DEVICE_NAME) - 1,
        .name_is_complete = 1,
    };

    ESP_LOGI(TAG, "Setting advertising data: UUID=0x1812, appearance=0x03C4, name='%s'",
             BLE_HID_DEVICE_NAME);

    int rc = ble_gap_adv_set_fields(&adv_fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to set advertising fields: %d", rc);
    } else {
        ESP_LOGI(TAG, "Advertising fields set successfully");
    }
}

// HID device event handler
static void hid_event_handler(void *handler_args, esp_event_base_t base, int32_t id, void *event_data)
{
    esp_hidd_event_t event = (esp_hidd_event_t)id;
    esp_hidd_event_data_t *param = (esp_hidd_event_data_t *)event_data;

    ESP_LOGI(TAG, "HID event: %d", event);

    switch (event) {
    case ESP_HIDD_START_EVENT:
        ESP_LOGI(TAG, "HID Device started, setting advertising data");

        // Set advertising data with HID service UUID and gamepad appearance
        set_hid_adv_data();

        // Small delay to ensure advertising data is set
        vTaskDelay(pdMS_TO_TICKS(100));

        // Start advertising
        struct ble_gap_adv_params adv_params = {
            .conn_mode = BLE_GAP_CONN_MODE_UND,
            .disc_mode = BLE_GAP_DISC_MODE_GEN,
            .itvl_min = 0x20,
            .itvl_max = 0x40,
        };
        int rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER, &adv_params, NULL, NULL);
        if (rc != 0) {
            ESP_LOGE(TAG, "Failed to start advertising: %d", rc);
        } else {
            ESP_LOGI(TAG, "Advertising started successfully - device: '%s'", BLE_HID_DEVICE_NAME);
            ESP_LOGI(TAG, "Device should now be visible as M5Stack-Joystick gamepad");
        }
        break;
    case ESP_HIDD_CONNECT_EVENT:
        ESP_LOGI(TAG, "HID Device connected");
        s_ble_connected = true;
        ESP_LOGI(TAG, "Ready to send HID reports");
        break;
    case ESP_HIDD_DISCONNECT_EVENT:
        ESP_LOGI(TAG, "HID Device disconnected, reason: %d", param->disconnect.reason);
        s_ble_connected = false;
        // Restart advertising on disconnect
        set_hid_adv_data();
        struct ble_gap_adv_params adv_params_disc = {
            .conn_mode = BLE_GAP_CONN_MODE_UND,
            .disc_mode = BLE_GAP_DISC_MODE_GEN,
            .itvl_min = 0x20,
            .itvl_max = 0x40,
        };
        ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER, &adv_params_disc, NULL, NULL);
        ESP_LOGI(TAG, "Advertising restarted");
        break;
    default:
        ESP_LOGI(TAG, "Unhandled HID event: %d", event);
        break;
    }
}

// BLE host task (required by NimBLE)
static void ble_host_task(void *param)
{
    ESP_LOGI(TAG, "BLE host task started - will run forever");
    nimble_port_run(); // This function will not return
    ESP_LOGE(TAG, "nimble_port_run returned unexpectedly!");
    nimble_port_freertos_deinit();
}

esp_err_t ble_hid_init(void)
{
    ESP_LOGI(TAG, "Initializing BLE HID with NimBLE stack...");

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize NimBLE
    ret = nimble_port_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize NimBLE: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "NimBLE initialized");

    // Initialize HID device BEFORE starting BLE host (critical for START_EVENT)
    ret = esp_hidd_dev_init(&ble_hid_config, ESP_HID_TRANSPORT_BLE, hid_event_handler, &s_ble_hid_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize HID device: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "HID device initialized");

    // Start BLE host with task function (this will trigger START_EVENT when synced)
    ret = esp_nimble_enable(ble_host_task);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable NimBLE: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "BLE HID initialized - waiting for START_EVENT to begin advertising");

    return ESP_OK;
}

esp_err_t ble_hid_deinit(void)
{
    ESP_LOGI(TAG, "Deinitializing BLE HID...");

    if (s_ble_hid_dev != NULL) {
        esp_hidd_dev_deinit(s_ble_hid_dev);
        s_ble_hid_dev = NULL;
    }

    esp_nimble_disable();
    nimble_port_deinit();
    nvs_flash_deinit();

    s_ble_connected = false;
    ESP_LOGI(TAG, "BLE HID deinitialized");
    return ESP_OK;
}

bool ble_hid_is_connected(void)
{
    return s_ble_connected;
}

void ble_hid_send_gamepad(uint16_t buttons, int8_t x1, int8_t y1, int8_t x2, int8_t y2)
{
    if (!s_ble_connected || s_ble_hid_dev == NULL) {
        return;
    }

    uint8_t report[BLE_GAMEPAD_REPORT_LEN];
    report[0] = buttons & 0xFF;
    report[1] = (buttons >> 8) & 0xFF;
    report[2] = (uint8_t)x1;
    report[3] = (uint8_t)y1;
    report[4] = (uint8_t)x2;
    report[5] = (uint8_t)y2;

    esp_err_t ret = esp_hidd_dev_input_set(s_ble_hid_dev, 0, BLE_RPT_ID_GAMEPAD, report, BLE_GAMEPAD_REPORT_LEN);
    if (ret != ESP_OK) {
        // Don't log error for ESP_FAIL (busy) - this is normal when sending too fast
        if (ret != ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to send gamepad report: %s", esp_err_to_name(ret));
        }
    }

    // Log report every ~1 second (every 50th report at 20ms poll rate)
    static int report_count = 0;
    if (++report_count >= 50) {
        ESP_LOGI(TAG, "Gamepad report: btn=0x%04x x1=%d y1=%d x2=%d y2=%d",
                 buttons, x1, y1, x2, y2);
        report_count = 0;
    }
}

void ble_hid_send_keyboard(uint8_t modifier, const uint8_t *keycodes, uint8_t num_keycodes)
{
    if (!s_ble_connected || s_ble_hid_dev == NULL) {
        return;
    }

    uint8_t report[8];
    report[0] = modifier;
    report[1] = 0;

    for (uint8_t i = 0; i < 6; i++) {
        report[2 + i] = (i < num_keycodes) ? keycodes[i] : 0;
    }

    esp_hidd_dev_input_set(s_ble_hid_dev, 0, 1, report, 8);
}

void ble_hid_send_mouse(uint8_t buttons, int8_t x, int8_t y)
{
    if (!s_ble_connected || s_ble_hid_dev == NULL) {
        return;
    }

    uint8_t report[4];
    report[0] = buttons & 0x07;
    report[1] = (uint8_t)x;
    report[2] = (uint8_t)y;
    report[3] = 0;

    esp_hidd_dev_input_set(s_ble_hid_dev, 0, 2, report, 4);
}
