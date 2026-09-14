/*
 * Hardware input reading for AtomS3 USB Joystick (Raylib port).
 *
 * Reads the I2C StampFly joystick unit (Joy1 + Joy2 axes, 4 I2C buttons) and
 * the AtomS3 built-in GPIO41 button. Results are written into a
 * hid_input_state_t snapshot that the HID report module consumes.
 */

#ifndef GPIO_INPUT_H
#define GPIO_INPUT_H

#include <stdint.h>
#include "hid_reports.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the I2C joystick + any input configuration.
 */
void gpio_input_init(void);

/**
 * @brief Read the current input state into *s.
 * @param s Snapshot to fill (must be non-NULL).
 */
void gpio_input_read(hid_input_state_t *s);

#ifdef __cplusplus
}
#endif

#endif // GPIO_INPUT_H
