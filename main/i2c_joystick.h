/*
 * I2C Joystick Driver for StampFly Controller
 * Ported from Arduino atoms3joy library to ESP-IDF
 */

#ifndef I2C_JOYSTICK_H
#define I2C_JOYSTICK_H

#include "esp_err.h"
#include "esp_log.h"
#include "driver/i2c_master.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// I2C Configuration
#define I2C_JOYSTICK_ADDR       0x59
#define I2C_SDA_GPIO            38
#define I2C_SCL_GPIO            39
#define I2C_FREQ_HZ             400000  // 400kHz Fast Mode
#define I2C_TIMEOUT_MS          100

// Joystick Registers
#define JOY1_X_REG              0x00
#define JOY1_Y_REG              0x02
#define JOY2_X_REG              0x20
#define JOY2_Y_REG              0x22
#define BUTTON_1_REG            0x70
#define BUTTON_2_REG            0x71
#define BUTTON_A_REG            0x72
#define BUTTON_B_REG            0x73
#define FIRMWARE_VERSION_REG    0xFE

// Button indices (matching atoms3joy.h)
#define BUTTON_LEFT             0  // BUTTON_1
#define BUTTON_RIGHT            1  // BUTTON_2
#define BUTTON_LEFT_STICK       2  // BUTTON_A
#define BUTTON_RIGHT_STICK      3  // BUTTON_B

typedef struct {
    i2c_master_bus_handle_t bus_handle;
    i2c_master_dev_handle_t dev_handle;
    bool initialized;
} i2c_joystick_handle_t;

/**
 * @brief Initialize I2C joystick driver
 *
 * @param stick Pointer to joystick handle
 * @return esp_err_t
 */
esp_err_t i2c_joystick_init(i2c_joystick_handle_t *stick);

/**
 * @brief Read joystick axis value (12-bit)
 *
 * @param stick Joystick handle
 * @param reg Register address (e.g., JOY1_X_REG)
 * @param value Pointer to store 16-bit value
 * @return esp_err_t
 */
esp_err_t i2c_joystick_read_axis(i2c_joystick_handle_t *stick, uint8_t reg, uint16_t *value);

/**
 * @brief Read button state (inverted: 0=pressed, 1=released)
 *
 * @param stick Joystick handle
 * @param button Button index (0-3)
 * @param pressed Pointer to store button state
 * @return esp_err_t
 */
esp_err_t i2c_joystick_read_button(i2c_joystick_handle_t *stick, uint8_t button, bool *pressed);

/**
 * @brief Read all joystick data at once
 *
 * @param stick Joystick handle
 * @param joy1_x Joy1 X axis (0-4095)
 * @param joy1_y Joy1 Y axis (0-4095)
 * @param btn_left LEFT button state
 * @param btn_right RIGHT button state
 * @param btn_left_stick LEFT_STICK button state
 * @param btn_right_stick RIGHT_STICK button state
 * @return esp_err_t
 */
esp_err_t i2c_joystick_read_all(i2c_joystick_handle_t *stick,
                                 uint16_t *joy1_x, uint16_t *joy1_y,
                                 bool *btn_left, bool *btn_right,
                                 bool *btn_left_stick, bool *btn_right_stick);

#ifdef __cplusplus
}
#endif

#endif // I2C_JOYSTICK_H
