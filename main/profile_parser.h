#ifndef PROFILE_PARSER_H
#define PROFILE_PARSER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_PROFILES 10
#define MAX_PROFILE_NAME 32
#define MAX_PROFILE_PATH 64

typedef enum {
    PROFILE_TYPE_GAMEPAD,
    PROFILE_TYPE_KEYBOARD,
    PROFILE_TYPE_KEYBOARD_MOUSE,  // Keyboard + Mouse composite
    PROFILE_TYPE_UNKNOWN
} profile_type_t;

typedef enum {
    CONNECTION_MODE_USB,      // USB HID connection
    CONNECTION_MODE_BLE,      // BLE HID connection
    CONNECTION_MODE_AUTO      // Auto-detect (default)
} connection_mode_t;

typedef struct {
    char name[MAX_PROFILE_NAME];
    profile_type_t type;
    connection_mode_t connection_mode;
    int version;
    char filename[MAX_PROFILE_PATH];
    bool is_valid;
    float joystick_threshold;  // Threshold for binary detection (0.0-1.0, default 0.5)
} profile_info_t;

/**
 * @brief Scan profiles directory and load all available profiles
 * @return Number of profiles loaded
 */
int profile_parser_load_profiles(void);

/**
 * @brief Get profile info by index
 * @param index Profile index (0 to MAX_PROFILES-1)
 * @return Pointer to profile info, or NULL if invalid
 */
const profile_info_t* profile_parser_get_profile(int index);

/**
 * @brief Get number of loaded profiles
 * @return Number of profiles
 */
int profile_parser_get_count(void);

/**
 * @brief Check if profile loading succeeded
 * @return true if profiles loaded, false if using defaults
 */
bool profile_parser_profiles_loaded(void);

/**
 * @brief Get number of profiles for specific connection mode
 * @param mode Connection mode (USB/BLE/AUTO)
 * @return Number of matching profiles
 */
int profile_parser_get_count_by_mode(connection_mode_t mode);

/**
 * @brief Get profile info by index for specific connection mode
 * @param mode Connection mode (USB/BLE/AUTO)
 * @param index Profile index within filtered list
 * @return Pointer to profile info, or NULL if invalid
 */
const profile_info_t* profile_parser_get_profile_by_mode(connection_mode_t mode, int index);

/**
 * @brief Convert connection mode string to enum
 * @param mode_str String ("usb", "ble", "auto")
 * @return Connection mode enum
 */
connection_mode_t profile_parser_parse_connection_mode(const char *mode_str);

#ifdef __cplusplus
}
#endif

#endif // PROFILE_PARSER_H
