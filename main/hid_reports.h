/*
 * HID report generation for AtomS3 USB Joystick (Raylib port).
 *
 * Ported from the reference firmware's gpio-keyboard.c (report generation
 * section) into an isolated module. This module owns:
 *   - the TinyUSB HID/MSC descriptors + current descriptor pointers,
 *   - USB + BLE report builders (gamepad / keyboard / mouse),
 *   - the TinyUSB device-event handler,
 *   - the TinyUSB callbacks (report descriptor, etc).
 *
 * It takes a snapshot of the sampled hardware input (hid_input_state_t) and
 * does not touch any hardware directly.
 */

#ifndef HID_REPORTS_H
#define HID_REPORTS_H

#include <stdint.h>
#include <stddef.h>
#include <esp_err.h>
#include "profile_parser.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Sampled hardware input. Filled by the input-reading module.
 *
 * Button flags use active-LOW logic mapped to "true = pressed".
 * Joystick axes are the raw 12-bit ADC values (0..4095).
 */
typedef struct {
    bool btn_left;            // I2C 0x70  -> Face button left
    bool btn_right;           // I2C 0x71  -> Face button right
    bool btn_left_stick;      // I2C 0x72  -> Left stick button
    bool btn_right_stick;     // I2C 0x73  -> Right stick button
    bool gpio_pressed;        // GPIO41 built-in button: one-frame "single click" pulse
    uint16_t joy1_x;          // Joy1 X axis raw
    uint16_t joy1_y;          // Joy1 Y axis raw
    uint16_t joy2_x;          // Joy2 X axis raw
    uint16_t joy2_y;          // Joy2 Y axis raw
} hid_input_state_t;

/**
 * @brief Configure HID descriptors + flags from the selected profile.
 * @param pinfo Selected profile info (may be NULL -> defaults to gamepad).
 *
 * Sets the report/config descriptor pointers and is_keyboard_profile /
 * is_mouse_profile flags used by the report builders.
 */
void hid_reports_setup(const profile_info_t *pinfo);

/**
 * @brief Get the current TinyUSB HID report descriptor + its length.
 */
const uint8_t *hid_reports_get_report_descriptor(size_t *len);

/**
 * @brief Get the current TinyUSB configuration descriptor + its length.
 */
const uint8_t *hid_reports_get_config_descriptor(size_t *len);

/**
 * @brief Get the TinyUSB MSC-only configuration descriptor + its length.
 *
 * Used to (re)install TinyUSB for Mass Storage mode where the HID report
 * descriptor is replaced by an MSC one.
 */
const uint8_t *hid_reports_get_msc_config_descriptor(size_t *len);

/**
 * @brief Get the TinyUSB string descriptor table + number of entries.
 */
const char **hid_reports_get_string_descriptor(size_t *count);

/**
 * @brief Install TinyUSB for HID mode using the current profile descriptor.
 *
 * Wraps tinyusb (kept internal to this module so the raylib/tinyusb
 * MOUSE_BUTTON enumerator clash never reaches the caller).
 * @return ESP_OK on success, else the tinyusb install error.
 */
esp_err_t hid_reports_install_usb_hid(void);

/**
 * @brief Install TinyUSB for Mass Storage mode (MSC descriptor).
 * @return ESP_OK on success, else the tinyusb install error.
 */
esp_err_t hid_reports_install_usb_msc(void);

/**
 * @brief True if the TinyUSB host has mounted the device.
 */
bool hid_reports_usb_mounted(void);

/* --- USB HID report sends --- */

/** @brief Build + send the USB gamepad report from the input snapshot. */
void hid_reports_send_gamepad(const hid_input_state_t *s);

/** @brief Build + send the USB keyboard report from the input snapshot. */
void hid_reports_send_keyboard(const hid_input_state_t *s);

/** @brief Build + send the USB mouse report from the input snapshot. */
void hid_reports_send_mouse(const hid_input_state_t *s);

/* --- BLE HID report sends --- */

void hid_reports_send_gamepad_ble(const hid_input_state_t *s);
void hid_reports_send_keyboard_ble(const hid_input_state_t *s);
void hid_reports_send_mouse_ble(const hid_input_state_t *s);

#ifdef __cplusplus
}
#endif

#endif // HID_REPORTS_H
