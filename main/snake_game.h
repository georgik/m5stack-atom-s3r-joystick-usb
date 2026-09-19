/*
 * Snake Game for AtomS3 USB Joystick
 *
 * Classic snake game implementation for joystick testing, written in Raylib.
 */

#ifndef SNAKE_GAME_H
#define SNAKE_GAME_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize snake game
 */
void snake_game_init(void);

/**
 * @brief Start snake game (initialises state; display is handled by caller)
 */
void snake_game_start(void);

/**
 * @brief Render the current snake game state (one frame).
 *
 * Called within the Raylib frame to draw the score, food and snake.
 */
void snake_game_render(void);

/**
 * @brief Check if snake game is currently active
 * @return true if game is running, false otherwise
 */
bool snake_game_is_active(void);

/**
 * @brief Handle input for snake game
 * @param joy1_up Joystick 1 up pressed
 * @param joy1_down Joystick 1 down pressed
 * @param joy1_left Joystick 1 left pressed
 * @param joy1_right Joystick 1 right pressed
 * @param button_a Joystick click (left stick button). Restart the game when it is
 *                 game over; pause/resume while playing. A rising edge is detected,
 *                 so a held button triggers exactly once.
 * @param button_b I2C LEFT face button. Exits the game back to the profile menu.
 * @return true if game should exit, false to continue
 */
bool snake_game_handle_input(bool joy1_up, bool joy1_down, bool joy1_left, bool joy1_right,
                             bool button_a, bool button_b);

/**
 * @brief Get current snake game score
 * @return Current score
 */
int snake_game_get_score(void);

/**
 * @brief Update game state (call this at game-loop rate)
 */
void snake_game_update(void);

/**
 * @brief Set the snake move period in milliseconds.
 *
 * The snake advances every this-many milliseconds of real time, independent of
 * the frame rate. Only values > 0 are accepted.
 *
 * @param ms Move interval in milliseconds
 */
void snake_game_set_move_interval_ms(int ms);

#ifdef __cplusplus
}
#endif

#endif // SNAKE_GAME_H
