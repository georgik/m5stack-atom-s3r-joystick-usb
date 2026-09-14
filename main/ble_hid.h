/*
 * BLE HID Implementation for M5Stack Atom Joystick USB
 */

#ifndef BLE_HID_H
#define BLE_HID_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BLE_HID_TAG "BLE_HID"
#define BLE_HID_DEVICE_NAME "M5Stack-Joystick"
#define BLE_RPT_ID_GAMEPAD 3
#define BLE_GAMEPAD_REPORT_LEN 6

/**
 * @brief Initialize BLE HID
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t ble_hid_init(void);

/**
 * @brief Deinitialize BLE HID
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t ble_hid_deinit(void);

/**
 * @brief Check if BLE HID is connected
 * @return true if connected, false otherwise
 */
bool ble_hid_is_connected(void);

/**
 * @brief Send gamepad report via BLE
 * @param buttons Button state (16 buttons)
 * @param x1 Left stick X (-127 to 127)
 * @param y1 Left stick Y (-127 to 127)
 * @param x2 Right stick X (-127 to 127)
 * @param y2 Right stick Y (-127 to 127)
 */
void ble_hid_send_gamepad(uint16_t buttons, int8_t x1, int8_t y1, int8_t x2, int8_t y2);

/**
 * @brief Send keyboard report via BLE
 * @param modifier Modifier keys (SHIFT, CTRL, etc.)
 * @param keycodes Array of keycodes (up to 6)
 * @param num_keycodes Number of keycodes in array
 */
void ble_hid_send_keyboard(uint8_t modifier, const uint8_t *keycodes, uint8_t num_keycodes);

/**
 * @brief Send mouse report via BLE
 * @param buttons Mouse buttons (bitmask)
 * @param x X movement
 * @param y Y movement
 */
void ble_hid_send_mouse(uint8_t buttons, int8_t x, int8_t y);

#ifdef __cplusplus
}
#endif

#endif // BLE_HID_H