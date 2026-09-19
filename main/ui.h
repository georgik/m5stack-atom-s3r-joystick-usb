/*
 * Raylib UI for AtomS3 USB Joystick
 *
 * Raylib draw-call implementation of the UI. All drawing is performed inside
 * the raylib_task frame between BeginDrawing()/EndDrawing(); the window is
 * 128x128.
 */

#ifndef UI_H
#define UI_H

#include <stdint.h>

/**
 * @brief Reset the USB Active uptime timer.
 */
void ui_reset_uptime(void);

/**
 * @brief Draw "Press to Start" screen.
 */
void ui_show_press_to_start(void);

/**
 * @brief Draw "USB Active" screen with profile name + live uptime.
 * @param profile_name Selected profile name (may be NULL)
 */
void ui_show_usb_active(const char *profile_name);

/**
 * @brief Draw an on-screen error message.
 *
 * Used for mode-init failures (USB/BLE/MSC). These cannot be debugged via
 * serial once the mode is active (the USB port is busy with HID, BLE has no
 * serial), so the error is shown on the display instead of only logged.
 *
 * @param line1  Primary message (e.g. "BLE HID")
 * @param line2  Secondary message (e.g. "init failed")
 */
void ui_show_error(const char *line1, const char *line2);

#endif // UI_H
