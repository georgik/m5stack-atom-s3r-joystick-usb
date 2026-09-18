/*
 * Profile Selection Menu for AtomS3 USB Joystick
 *
 * Raylib draw-call implementation of the profile menu. profile_menu_render() is
 * called each frame from the raylib_task frame; profile_menu_show() only
 * initialises state (load profiles, reset index).
 */

#include "profile_menu.h"
#include "profile_parser.h"
#include <raylib.h>
#include "esp_log.h"

static const char *TAG = "PROFILE_MENU";

// Menu state
static int s_selected_index = 0;
static bool s_menu_active = false;

void profile_menu_show(void)
{
    ESP_LOGI(TAG, "Showing profile menu...");

    // Load profiles from storage (or use built-in defaults)
    int profile_count = profile_parser_load_profiles();

    if (profile_parser_profiles_loaded()) {
        ESP_LOGI(TAG, "Loaded profiles from storage");
    } else {
        ESP_LOGI(TAG, "Using built-in default profiles");
    }

    // Reset selection
    s_selected_index = 0;
    s_menu_active = true;

    ESP_LOGI(TAG, "Profile menu ready, %d profiles available", profile_count);
}

void profile_menu_render(void)
{
    if (!s_menu_active) {
        return;
    }

    const int w = GetScreenWidth();
    const int h = GetScreenHeight();
    const int item_h = 20;
    const int fs = 9;
    const int margin_top = 20;      // y where the first list row starts
    const int margin_bottom = 4;    // leave a few px at the panel bottom
    const int count = profile_parser_get_count();

    // How many rows fit in the panel before we must scroll.
    const int max_visible = (h - margin_top - margin_bottom) / item_h;
    if (max_visible < 1) return;

    // Map the selected profile to its position among the valid rows.
    int sel_row = 0;
    int valid_total = 0;
    for (int i = 0; i < count; i++) {
        const profile_info_t *p = profile_parser_get_profile(i);
        if (p == NULL || !p->is_valid) {
            continue;
        }
        if (i == s_selected_index) {
            sel_row = valid_total;
        }
        valid_total++;
    }

    // Scroll so the selected row is always in view, clamping to the ends.
    int top = 0;
    if (sel_row < top) {
        top = sel_row;
    } else if (sel_row > top + max_visible - 1) {
        top = sel_row - max_visible + 1;
    }
    if (top < 0) top = 0;
    if (top > valid_total - max_visible) top = valid_total - max_visible;

    // Title
    const char *title = "Select Profile";
    DrawText(title, (w - MeasureText(title, fs)) / 2, 4, fs, WHITE);

    // List of profiles (only the rows within the scroll window are drawn).
    int row = 0;
    for (int i = 0; i < count; i++) {
        const profile_info_t *profile = profile_parser_get_profile(i);
        if (profile == NULL || !profile->is_valid) {
            continue;
        }

        if (row < top || row >= top + max_visible) {
            row++;
            continue;
        }

        const int y = margin_top + (row - top) * item_h;

        if (row == sel_row) {
            // Highlight the active row clearly: bright bar + arrow cursor.
            DrawRectangle(2, y, w - 4, item_h, CLITERAL(Color){ 0, 200, 255, 255 });
            DrawText(">", 4, y + 1, fs + 1, CLITERAL(Color){ 0, 40, 60, 255 });
            DrawText(profile->name, 14, y + 3, fs - 1, CLITERAL(Color){ 0, 40, 60, 255 });
        } else {
            DrawRectangle(2, y, w - 4, item_h, CLITERAL(Color){ 30, 30, 30, 255 });
            DrawText(profile->name, 14, y + 4, fs - 1, CLITERAL(Color){ 210, 210, 210, 255 });
        }
        row++;
    }
}

bool profile_menu_handle_input(bool joy1_up, bool joy1_down, bool button_pressed)
{
    if (!s_menu_active) {
        return false;
    }

    bool selection_made = false;
    const int profile_count = profile_parser_get_count();

    // Handle navigation
    if (joy1_up) {
        if (s_selected_index > 0) {
            s_selected_index--;
            const profile_info_t *profile = profile_parser_get_profile(s_selected_index);
            ESP_LOGD(TAG, "Selection moved up: %s", profile ? profile->name : "unknown");
        }
    } else if (joy1_down) {
        if (s_selected_index < profile_count - 1) {
            s_selected_index++;
            const profile_info_t *profile = profile_parser_get_profile(s_selected_index);
            ESP_LOGD(TAG, "Selection moved down: %s", profile ? profile->name : "unknown");
        }
    }

    // Handle selection
    if (button_pressed) {
        const profile_info_t *profile = profile_parser_get_profile(s_selected_index);
        ESP_LOGI(TAG, "Profile selected: %s", profile ? profile->name : "unknown");
        selection_made = true;
        s_menu_active = false;
    }

    return selection_made;
}

const char *profile_menu_get_selected(void)
{
    const profile_info_t *profile = profile_parser_get_profile(s_selected_index);
    if (profile != NULL) {
        return profile->name;
    }
    return NULL;
}

profile_type_t profile_menu_get_selected_type(void)
{
    const profile_info_t *profile = profile_parser_get_profile(s_selected_index);
    if (profile != NULL) {
        return profile->type;
    }
    return PROFILE_TYPE_UNKNOWN;
}

const profile_info_t *profile_menu_get_selected_info(void)
{
    return profile_parser_get_profile(s_selected_index);
}

bool profile_menu_is_active(void)
{
    return s_menu_active;
}

void profile_menu_hide(void)
{
    s_menu_active = false;
    s_selected_index = 0;

    ESP_LOGI(TAG, "Profile menu hidden");
}

bool profile_menu_is_mass_storage_selected(void)
{
    const char *selected = profile_menu_get_selected();
    return (selected != NULL && strcmp(selected, "Mass Storage") == 0);
}

bool profile_menu_is_snake_game_selected(void)
{
    const char *selected = profile_menu_get_selected();
    return (selected != NULL && strcmp(selected, "Snake Game") == 0);
}
