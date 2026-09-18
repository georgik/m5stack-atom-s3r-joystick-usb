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

#endif // UI_H
