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
#include <stdlib.h>
#include <stdio.h>

static const char *TAG = "SNAKE_GAME";

// Game constants
#define GRID_SIZE 16
#define CELL_SIZE 8
#define SNAKE_INITIAL_LENGTH 3
#define SNAKE_MOVE_INTERVAL 4          // Move snake every 4 frames (~400ms)

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
static int frame_count = 0;           // Frame counter for snake movement timing

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

    // Only move snake every N frames to control speed
    frame_count++;
    if (frame_count % SNAKE_MOVE_INTERVAL != 0) {
        return;  // Skip movement this frame
    }

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

    // Check wall collision
    if (new_head.x < 0 || new_head.x >= GRID_SIZE ||
        new_head.y < 0 || new_head.y >= GRID_SIZE) {
        game_over = true;
        ESP_LOGI(TAG, "Wall collision! Game over. Score: %d", score);
        return;
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
    frame_count = 0;
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
    frame_count = 0;  // Reset frame counter

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

    // Handle pause/resume
    if (button_a && !game_over) {
        game_paused = !game_paused;
        ESP_LOGI(TAG, "Game %s", game_paused ? "paused" : "resumed");
        return false;
    }

    // Exit game on button B
    if (button_b) {
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

    // Restart on button A if game over
    if (button_a && game_over) {
        ESP_LOGI(TAG, "Restarting game");
        snake_game_start();
        return false;
    }

    return false; // Continue game
}

int snake_game_get_score(void)
{
    return score;
}

void snake_game_update(void)
{
    update_game();
}
