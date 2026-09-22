/*
 * WS2812 RGB LED control for the Atom JoyStick sub-board.
 *
 * The 2x WS2812C LEDs near the L/R buttons are wired DIRECTLY to the AtomS3
 * (ESP32-S3) on GPIO6. The public m5stack AtomJoyStick/atoms3joy libraries only read
 * sticks/buttons/battery, which is why chasing an I2C LED command to 0x59
 * never worked. We instead drive GPIO6 with the standard RMT-based led_strip
 * driver (one-wire WS2812 protocol).
 *
 * See wiki/handover.md "Atom JoyStick sub-board LEDs (WS2812) -- feasibility".
 */

#ifndef JOY_LED_H
#define JOY_LED_H

#include <stdint.h>

/**
 * @brief Initialize the RMT LED strip on GPIO6 (2x WS2812C).
 */
void joy_led_init(void);

/**
 * @brief Set both base LEDs to a fixed color.
 */
void joy_led_set(uint8_t r, uint8_t g, uint8_t b);

/**
 * @brief Turn both base LEDs off.
 */
void joy_led_clear(void);

/**
 * @brief Drive both LEDs with a sweeping rainbow (full HSV hue wheel).
 *
 * The phase is derived from the FreeRTOS tick counter internally, so it does
 * not depend on raylib's GetTime() (which is unreliable on ESP32). The hue
 * completes one full circle every ~2 seconds regardless of call rate.
 *
 * Runs at JOY_LED_BRIGHTNESS. No-op if the strip has not been initialized.
 */
void joy_led_update_rainbow(void);

/**
 * @brief Smoothly fade the base LEDs up to JOY_LED_BRIGHTNESS (a "glow").
 *
 * Linear ramp from off to the target brightness over ~0.5 s. Blocks via
 * vTaskDelay, so call it from app_main before the UI.
 */
void joy_led_glow(void);

#endif // JOY_LED_H
