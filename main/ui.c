/*
 * Raylib UI for AtomS3 USB Joystick
 *
 * Conversion of the original LVGL UI (ui_lvgl.c) to Raylib draw calls.
 *
 * IMPORTANT: these functions only issue Raylib draw commands. They must be
 * called from inside the raylib_task frame (between BeginDrawing()/EndDrawing()).
 */

#include "ui.h"
#include <raylib.h>
#include "esp_log.h"
#include <stdio.h>
#include <string.h>
#include <sys/time.h>

static const char *TAG = "UI_RAYLIB";

// USB Active uptime timer (microseconds)
static int64_t s_usb_start_us = 0;

void ui_reset_uptime(void)
{
    s_usb_start_us = 0;
}

void ui_show_press_to_start(void)
{
    const int w = GetScreenWidth();
    const int h = GetScreenHeight();
    const int fs = 10;

    const char *title = "Press to Start";
    DrawText(title, (w - MeasureText(title, fs)) / 2, 36, fs, WHITE);

    const char *sub = "USB Joystick Mode";
    DrawText(sub, (w - MeasureText(sub, fs)) / 2, 58, fs, DARKGRAY);

    const char *info = "GPIO 41 or any button";
    DrawText(info, (w - MeasureText(info, fs)) / 2, 92, fs, GRAY);

    ESP_LOGI(TAG, "UI: Press to Start");
}

void ui_show_usb_active(const char *profile_name)
{
    const int w = GetScreenWidth();
    const int fs = 10;

    // Capture the session start time once
    if (s_usb_start_us == 0) {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        s_usb_start_us = (int64_t)tv.tv_sec * 1000000LL + tv.tv_usec;
        ESP_LOGI(TAG, "UI: USB Active session started (profile=%s)",
                 profile_name ? profile_name : "unknown");
    }

    const char *title = "USB Active";
    DrawText(title, (w - MeasureText(title, fs)) / 2, 18, fs, GREEN);

    if (profile_name != NULL && *profile_name != '\0') {
        char buf[64];
        snprintf(buf, sizeof(buf), "Profile: %s", profile_name);
        DrawText(buf, (w - MeasureText(buf, fs)) / 2, 40, fs, DARKGRAY);
    }

    const char *status =
        (profile_name != NULL && strcmp(profile_name, "Mass Storage") == 0)
            ? "USB Drive Ready"
            : "Joystick Ready";
    DrawText(status, (w - MeasureText(status, fs)) / 2, 64, fs, WHITE);

    struct timeval tv;
    gettimeofday(&tv, NULL);
    int64_t now_us = (int64_t)tv.tv_sec * 1000000LL + tv.tv_usec;
    int elapsed_sec = (int)((now_us - s_usb_start_us) / 1000000LL);
    if (elapsed_sec < 0) elapsed_sec = 0;

    char ubuf[40];
    snprintf(ubuf, sizeof(ubuf), "Uptime: %ds", elapsed_sec);
    DrawText(ubuf, (w - MeasureText(ubuf, fs)) / 2, 88, fs, GRAY);
}
