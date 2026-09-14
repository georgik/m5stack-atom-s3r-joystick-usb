/*
 * Profile Parser for AtomS3 USB Joystick
 *
 * Scans /storage/profiles/ directory for .ini files and parses them
 * to extract profile information. Falls back to built-in defaults if
 * files are missing or invalid.
 */

#include "profile_parser.h"
#include "esp_log.h"
#include "esp_err.h"
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

static const char *TAG = "PROFILE_PARSER";

static profile_info_t s_profiles[MAX_PROFILES];
static int s_profile_count = 0;
static bool s_profiles_loaded = false;

// Built-in default profiles (fallback)
static const profile_info_t s_default_profiles[] = {
    {.name = "gamepad", .type = PROFILE_TYPE_GAMEPAD, .connection_mode = CONNECTION_MODE_USB, .version = 1, .filename = "", .is_valid = true},
    {.name = "wasd", .type = PROFILE_TYPE_KEYBOARD_MOUSE, .connection_mode = CONNECTION_MODE_USB, .version = 1, .filename = "", .is_valid = true},
    {.name = "arrows", .type = PROFILE_TYPE_KEYBOARD, .connection_mode = CONNECTION_MODE_USB, .version = 1, .filename = "", .is_valid = true},
    {.name = "Mass Storage", .type = PROFILE_TYPE_UNKNOWN, .connection_mode = CONNECTION_MODE_USB, .version = 1, .filename = "", .is_valid = true},
    {.name = "Snake Game", .type = PROFILE_TYPE_UNKNOWN, .connection_mode = CONNECTION_MODE_AUTO, .version = 1, .filename = "", .is_valid = true},
};
static const int s_default_count = sizeof(s_default_profiles) / sizeof(s_default_profiles[0]);

static profile_type_t parse_profile_type(const char *type_str)
{
    if (type_str == NULL) return PROFILE_TYPE_UNKNOWN;

    if (strcasecmp(type_str, "gamepad") == 0) {
        return PROFILE_TYPE_GAMEPAD;
    } else if (strcasecmp(type_str, "keyboard_mouse") == 0) {
        return PROFILE_TYPE_KEYBOARD_MOUSE;
    } else if (strcasecmp(type_str, "keyboard") == 0) {
        return PROFILE_TYPE_KEYBOARD;
    }

    return PROFILE_TYPE_UNKNOWN;
}

static bool parse_ini_file(const char *filepath, profile_info_t *profile)
{
    FILE *f = fopen(filepath, "r");
    if (f == NULL) {
        ESP_LOGW(TAG, "Failed to open file: %s", filepath);
        return false;
    }

    char line[256];
    bool in_profile_section = false;
    bool found_name = false;
    bool found_type = false;

    while (fgets(line, sizeof(line), f) != NULL) {
        // Remove trailing newline
        line[strcspn(line, "\r\n")] = 0;

        // Skip empty lines and comments
        if (line[0] == '\0' || line[0] == ';' || line[0] == '#') {
            continue;
        }

        // Check for section header
        if (line[0] == '[') {
            char *end = strchr(line, ']');
            if (end != NULL) {
                *end = '\0';
                in_profile_section = (strcasecmp(line + 1, "profile") == 0);
            }
            continue;
        }

        // Parse key = value pairs in [profile] section
        if (in_profile_section) {
            char *eq = strchr(line, '=');
            if (eq != NULL) {
                *eq = '\0';
                char *key = line;
                char *value = eq + 1;

                // Trim whitespace
                while (*key == ' ' || *key == '\t') key++;
                while (*value == ' ' || *value == '\t') value++;

                char *key_end = key + strlen(key) - 1;
                while (key_end > key && (*key_end == ' ' || *key_end == '\t')) {
                    *key_end-- = '\0';
                }

                // Parse profile fields
                if (strcasecmp(key, "name") == 0) {
                    strncpy(profile->name, value, MAX_PROFILE_NAME - 1);
                    profile->name[MAX_PROFILE_NAME - 1] = '\0';
                    found_name = true;
                } else if (strcasecmp(key, "type") == 0) {
                    profile->type = parse_profile_type(value);
                    found_type = true;
                } else if (strcasecmp(key, "connection_mode") == 0) {
                    profile->connection_mode = profile_parser_parse_connection_mode(value);
                } else if (strcasecmp(key, "version") == 0) {
                    profile->version = atoi(value);
                } else if (strcasecmp(key, "joystick_threshold") == 0) {
                    profile->joystick_threshold = atof(value);
                }
            }
        }
    }

    fclose(f);

    // Profile is valid if it has at least a name
    if (found_name) {
        if (!found_type) {
            // Default to gamepad if type not specified
            profile->type = PROFILE_TYPE_GAMEPAD;
        }
        if (profile->version == 0) {
            profile->version = 1;
        }
        if (profile->joystick_threshold == 0.0f) {
            // Default threshold if not specified
            profile->joystick_threshold = 0.5f;
        }
        if (profile->connection_mode == CONNECTION_MODE_AUTO) {
            // Default to USB if connection mode not specified
            profile->connection_mode = CONNECTION_MODE_USB;
        }
        return true;
    }

    return false;
}

int profile_parser_load_profiles(void)
{
    ESP_LOGI(TAG, "Loading profiles from /storage/profiles/...");

    DIR *dir = opendir("/storage/profiles");
    if (dir == NULL) {
        ESP_LOGW(TAG, "Failed to open /storage/profiles/, using defaults");
        goto load_defaults;
    }

    struct dirent *entry;
    s_profile_count = 0;

    while ((entry = readdir(dir)) != NULL && s_profile_count < MAX_PROFILES) {
        // Skip files that don't end with .ini
        size_t len = strlen(entry->d_name);
        if (len < 4 || strcmp(entry->d_name + len - 4, ".ini") != 0) {
            continue;
        }

        // Build full file path
        char filepath[512];
        snprintf(filepath, sizeof(filepath), "/storage/profiles/%s", entry->d_name);

        // Parse the profile file
        profile_info_t profile = {0};
        if (parse_ini_file(filepath, &profile)) {
            // Store the filename
            strncpy(profile.filename, entry->d_name, MAX_PROFILE_PATH - 1);
            profile.filename[MAX_PROFILE_PATH - 1] = '\0';
            profile.is_valid = true;

            s_profiles[s_profile_count] = profile;
            ESP_LOGI(TAG, "Loaded profile: %s (type=%d, version=%d, file=%s)",
                     profile.name, profile.type, profile.version, profile.filename);
            ESP_LOGI(TAG, "  Threshold: %.2f", profile.joystick_threshold);
            s_profile_count++;
        } else {
            ESP_LOGW(TAG, "Failed to parse profile file: %s", entry->d_name);
        }
    }

    closedir(dir);

    if (s_profile_count > 0) {
        // Add Mass Storage option at the end
        if (s_profile_count < MAX_PROFILES) {
            profile_info_t mass_storage = {
                .name = "Mass Storage",
                .type = PROFILE_TYPE_UNKNOWN,
                .connection_mode = CONNECTION_MODE_USB,
                .version = 1,
                .filename = "",
                .is_valid = true
            };
            s_profiles[s_profile_count] = mass_storage;
            s_profile_count++;
            ESP_LOGI(TAG, "Added Mass Storage system option");
        }

        // Add Snake Game option at the end
        if (s_profile_count < MAX_PROFILES) {
            profile_info_t snake_game = {
                .name = "Snake Game",
                .type = PROFILE_TYPE_UNKNOWN,
                .connection_mode = CONNECTION_MODE_AUTO,
                .version = 1,
                .filename = "",
                .is_valid = true
            };
            s_profiles[s_profile_count] = snake_game;
            s_profile_count++;
            ESP_LOGI(TAG, "Added Snake Game system option");
        }

        s_profiles_loaded = true;
        ESP_LOGI(TAG, "Successfully loaded %d profiles from storage", s_profile_count);
        return s_profile_count;
    }

    ESP_LOGW(TAG, "No valid profiles found in storage, using defaults");

load_defaults:
    // Fall back to built-in defaults
    for (int i = 0; i < s_default_count && i < MAX_PROFILES; i++) {
        s_profiles[i] = s_default_profiles[i];
    }
    s_profile_count = s_default_count;
    s_profiles_loaded = false;

    ESP_LOGI(TAG, "Using %d built-in default profiles", s_profile_count);
    return s_profile_count;
}

const profile_info_t* profile_parser_get_profile(int index)
{
    if (index >= 0 && index < s_profile_count) {
        return &s_profiles[index];
    }
    return NULL;
}

int profile_parser_get_count(void)
{
    return s_profile_count;
}

bool profile_parser_profiles_loaded(void)
{
    return s_profiles_loaded;
}

connection_mode_t profile_parser_parse_connection_mode(const char *mode_str)
{
    if (mode_str == NULL) return CONNECTION_MODE_AUTO;

    if (strcasecmp(mode_str, "usb") == 0) {
        return CONNECTION_MODE_USB;
    } else if (strcasecmp(mode_str, "ble") == 0) {
        return CONNECTION_MODE_BLE;
    } else if (strcasecmp(mode_str, "auto") == 0) {
        return CONNECTION_MODE_AUTO;
    }

    return CONNECTION_MODE_AUTO;  // Default to AUTO
}

int profile_parser_get_count_by_mode(connection_mode_t mode)
{
    if (mode == CONNECTION_MODE_AUTO) {
        // AUTO mode returns all profiles
        return s_profile_count;
    }

    int count = 0;
    for (int i = 0; i < s_profile_count; i++) {
        if (s_profiles[i].connection_mode == mode || s_profiles[i].connection_mode == CONNECTION_MODE_AUTO) {
            count++;
        }
    }
    return count;
}

const profile_info_t* profile_parser_get_profile_by_mode(connection_mode_t mode, int index)
{
    if (mode == CONNECTION_MODE_AUTO) {
        // AUTO mode returns all profiles
        return profile_parser_get_profile(index);
    }

    int filtered_index = 0;
    for (int i = 0; i < s_profile_count; i++) {
        if (s_profiles[i].connection_mode == mode || s_profiles[i].connection_mode == CONNECTION_MODE_AUTO) {
            if (filtered_index == index) {
                return &s_profiles[i];
            }
            filtered_index++;
        }
    }
    return NULL;
}
