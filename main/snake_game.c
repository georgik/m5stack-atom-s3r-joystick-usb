/*
 * Snake Game for AtomS3 USB Joystick
 *
 * Raylib draw-call implementation of the snake game. Snake_game_render() is
 * called each frame from the raylib_task frame. Snake_game_start() only sets up
 * state; it does not draw (no active frame).
 */

#include "snake_game.h"
#include <raylib.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdlib.h>
#include <stdio.h>

static const char *TAG = "SNAKE_GAME";

// Game constants
#define GRID_SIZE 16
#define CELL_SIZE 8
#define SNAKE_INITIAL_LENGTH 3
/*
 * Snake movement is driven by REAL elapsed time, not by the frame count.
 * The snake advances every SNAKE_MOVE_INTERVAL_MS of wall-clock time, so its
 * speed stays constant even if the host CPU / frame rate varies (a frame-count
 * interval would instead speed up or slow down with the loop rate).
 * Timing uses the FreeRTOS tick clock (xTaskGetTickCount / pdMS_TO_TICKS), which
 * is independent of the loop rate. NOTE: raylib's GetTime() on this esp-idf port
 * is a stub that always returns 0, so it CANNOT be used for timing here.
 * Classic snake pace: ~6.7 moves per second.
 */
#define SNAKE_MOVE_INTERVAL_MS 150

// Direction enumeration
typedef enum {
    DIR_UP,
    DIR_DOWN,
    DIR_LEFT,
    DIR_RIGHT
} direction_t;

// Position structure
typedef struct {
    int x;
    int y;
} position_t;

// Game state
static position_t snake[256]; // Maximum snake length
static int snake_length = 0;
static direction_t current_direction = DIR_RIGHT;
static direction_t next_direction = DIR_RIGHT;
static position_t food = {0, 0};
static int score = 0;
static bool game_active = false;
static bool game_over = false;
static bool game_paused = false;
static TickType_t last_move_tick = 0;             // xTaskGetTickCount() of the last snake move
static int move_interval_ms = SNAKE_MOVE_INTERVAL_MS;  // Configurable move period (ms)

/**
 * @brief Generate random position for food
 */
static void spawn_food(void)
{
    bool valid_position = false;

    while (!valid_position) {
        food.x = rand() % GRID_SIZE;
        food.y = rand() % GRID_SIZE;

        // Check if food spawns on snake body
        valid_position = true;
        for (int i = 0; i < snake_length; i++) {
            if (snake[i].x == food.x && snake[i].y == food.y) {
                valid_position = false;
                break;
            }
        }
    }

    ESP_LOGI(TAG, "Food spawned at (%d, %d)", food.x, food.y);
}

/**
 * @brief Draw game state using Raylib primitives (called within a frame).
 */
void snake_game_render(void)
{
    const int w = GetScreenWidth();
    const int fs = 10;

    // Score / message label at top
    char sbuf[40];
    if (game_over) {
        snprintf(sbuf, sizeof(sbuf), "GAME OVER: %d", score);
    } else if (game_paused) {
        snprintf(sbuf, sizeof(sbuf), "PAUSED: %d", score);
    } else {
        snprintf(sbuf, sizeof(sbuf), "Score: %d", score);
    }
    DrawText(sbuf, (w - MeasureText(sbuf, fs)) / 2, 2, fs, WHITE);

    // Draw food (yellow)
    DrawRectangle(food.x * CELL_SIZE, food.y * CELL_SIZE,
                  CELL_SIZE - 1, CELL_SIZE - 1, YELLOW);

    // Draw snake segments (green)
    for (int i = 0; i < snake_length; i++) {
        DrawRectangle(snake[i].x * CELL_SIZE, snake[i].y * CELL_SIZE,
                      CELL_SIZE - 1, CELL_SIZE - 1, GREEN);
    }
}

/**
 * @brief Update game state (called at game-loop rate).
 */
static void update_game(void)
{
    if (game_over || game_paused) {
        return;
    }

    // Move the snake on a fixed REAL-time schedule (robust to frame-rate / HW
    // variation). Use the FreeRTOS tick clock: GetTime() on this esp-idf port is a
    // stub that always returns 0, so xTaskGetTickCount() is the reliable clock.
    const TickType_t now = xTaskGetTickCount();
    if ((now - last_move_tick) < pdMS_TO_TICKS(move_interval_ms)) {
        return;  // Not enough elapsed time for the next move yet
    }
    last_move_tick = now;

    // Apply queued direction
    current_direction = next_direction;

    // Calculate new head position
    position_t new_head = snake[0];

    switch (current_direction) {
        case DIR_UP:
            new_head.y--;
            break;
        case DIR_DOWN:
            new_head.y++;
            break;
        case DIR_LEFT:
            new_head.x--;
            break;
        case DIR_RIGHT:
            new_head.x++;
            break;
    }

    // Wrap the head around the walls (no solid walls) so the snake continues on
    // the opposite side. This keeps the game playable on the tiny 128x128 screen;
    // the only way to lose is now to run into yourself.
    if (new_head.x < 0) {
        new_head.x = GRID_SIZE - 1;
    } else if (new_head.x >= GRID_SIZE) {
        new_head.x = 0;
    }
    if (new_head.y < 0) {
        new_head.y = GRID_SIZE - 1;
    } else if (new_head.y >= GRID_SIZE) {
        new_head.y = 0;
    }

    // Check self collision
    for (int i = 0; i < snake_length; i++) {
        if (snake[i].x == new_head.x && snake[i].y == new_head.y) {
            game_over = true;
            ESP_LOGI(TAG, "Self collision! Game over. Score: %d", score);
            return;
        }
    }

    // Move snake (add new head)
    memmove(&snake[1], &snake[0], sizeof(position_t) * snake_length);
    snake[0] = new_head;

    // Check food collision
    if (new_head.x == food.x && new_head.y == food.y) {
        // Grow snake
        snake_length++;
        score += 10;
        ESP_LOGI(TAG, "Food eaten! Score: %d, Length: %d", score, snake_length);
        spawn_food();
    }
}

void snake_game_init(void)
{
    ESP_LOGI(TAG, "Initializing Snake game");
    game_active = false;
    game_over = false;
    game_paused = false;
    last_move_tick = 0;
}

void snake_game_start(void)
{
    ESP_LOGI(TAG, "Starting Snake game");

    // Initialize game state
    snake_length = SNAKE_INITIAL_LENGTH;
    current_direction = DIR_RIGHT;
    next_direction = DIR_RIGHT;
    score = 0;
    game_over = false;
    game_paused = false;
    game_active = true;
    last_move_tick = xTaskGetTickCount();  // First move happens one interval from now

    // Initialize snake position (center of screen)
    snake[0].x = GRID_SIZE / 2;
    snake[0].y = GRID_SIZE / 2;
    for (int i = 1; i < snake_length; i++) {
        snake[i].x = snake[0].x - i;
        snake[i].y = snake[0].y;
    }

    // Spawn first food (ensure it's not on snake)
    bool food_valid = false;
    while (!food_valid) {
        food.x = rand() % GRID_SIZE;
        food.y = rand() % GRID_SIZE;

        food_valid = true;
        for (int i = 0; i < snake_length; i++) {
            if (snake[i].x == food.x && snake[i].y == food.y) {
                food_valid = false;
                break;
            }
        }
    }
    ESP_LOGI(TAG, "Initial food position: (%d, %d)", food.x, food.y);
}

bool snake_game_is_active(void)
{
    return game_active;
}

bool snake_game_handle_input(bool joy1_up, bool joy1_down, bool joy1_left, bool joy1_right,
                             bool button_a, bool button_b)
{
    if (!game_active) {
        return false;
    }

    // Convert the raw button levels into single-frame rising edges so a held
    // button triggers an action exactly once (a level would re-toggle / re-start
    // every frame).
    static bool btn_a_last = false;
    static bool btn_b_last = false;
    bool a_pressed = button_a && !btn_a_last;
    bool b_pressed = button_b && !btn_b_last;
    btn_a_last = button_a;
    btn_b_last = button_b;

    // button_a (joystick click): restart on game over, otherwise pause/resume.
    if (a_pressed) {
        if (game_over) {
            ESP_LOGI(TAG, "Restarting game");
            snake_game_start();
            return false;
        }
        game_paused = !game_paused;
        ESP_LOGI(TAG, "Game %s", game_paused ? "paused" : "resumed");
        return false;
    }

    // button_b (I2C LEFT face button): exit the game back to the profile menu.
    if (b_pressed) {
        ESP_LOGI(TAG, "Exiting Snake game - Score: %d", score);
        game_active = false;
        return true; // Signal to exit game
    }

    // Handle direction changes (prevent 180-degree turns)
    if (!game_paused && !game_over) {
        if (joy1_up && current_direction != DIR_DOWN) {
            next_direction = DIR_UP;
        } else if (joy1_down && current_direction != DIR_UP) {
            next_direction = DIR_DOWN;
        } else if (joy1_left && current_direction != DIR_RIGHT) {
            next_direction = DIR_LEFT;
        } else if (joy1_right && current_direction != DIR_LEFT) {
            next_direction = DIR_RIGHT;
        }
    }

    return false; // Continue game
}

int snake_game_get_score(void)
{
    return score;
}

/**
 * @brief Set the snake move period in milliseconds.
 *
 * The snake advances every this-many milliseconds of real time, independent of
 * the frame rate. Call before or while the game runs to tune the difficulty.
 * Only values > 0 are accepted.
 */
void snake_game_set_move_interval_ms(int ms)
{
    if (ms > 0) {
        move_interval_ms = ms;
    }
}

void snake_game_update(void)
{
    update_game();
}
