#ifndef PROFILE_MENU_H
#define PROFILE_MENU_H

#include <stdint.h>
#include <stdbool.h>
#include "profile_parser.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Show profile selection menu
 *
 * Loads profiles and initialises menu state. Must be called once before
 * profile_menu_render(); the menu is then drawn each frame by the caller.
 */
void profile_menu_show(void);

/**
 * @brief Render the profile selection menu (one frame).
 *
 * Draws the title and profile list to the current Raylib frame. Must be
 * called every frame while the menu is active.
 */
void profile_menu_render(void);

/**
 * @brief Handle joystick input for menu navigation
 *
 * @param joy1_up True if joy1 pushed up
 * @param joy1_down True if joy1 pushed down
 * @param button_pressed True if any button pressed (confirm selection)
 * @return true if profile selected, false if still in menu
 */
bool profile_menu_handle_input(bool joy1_up, bool joy1_down, bool button_pressed);

/**
 * @brief Get selected profile name
 *
 * @return Pointer to selected profile name (valid until next menu show)
 */
const char* profile_menu_get_selected(void);

/**
 * @brief Get selected profile type
 *
 * @return Profile type (gamepad, keyboard, or unknown)
 */
profile_type_t profile_menu_get_selected_type(void);

/**
 * @brief Get selected profile info structure
 *
 * @return Pointer to profile info, or NULL if invalid
 */
const profile_info_t* profile_menu_get_selected_info(void);

/**
 * @brief Check if menu is active
 *
 * @return true if menu is currently displayed
 */
bool profile_menu_is_active(void);

/**
 * @brief Hide profile menu
 */
void profile_menu_hide(void);

/**
 * @brief Check if "Mass Storage" was selected
 *
 * @return true if mass storage mode selected
 */
bool profile_menu_is_mass_storage_selected(void);

/**
 * @brief Check if "Snake Game" was selected
 *
 * @return true if snake game selected
 */
bool profile_menu_is_snake_game_selected(void);

#ifdef __cplusplus
}
#endif

#endif // PROFILE_MENU_H
