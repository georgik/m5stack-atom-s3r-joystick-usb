/*
 * Hardware input reading for AtomS3 USB Joystick (Raylib port).
 *
 * Reads the I2C StampFly joystick unit (Joy1 + Joy2 axes and the 4 I2C
 * face buttons) and the AtomS3 built-in GPIO41 button. Results are written
 * into a hid_input_state_t snapshot that hid_reports.c consumes.
 *
 * Mirrors the reference firmware's configure_i2c_joystick() + read_all() /
 * read_axis() flow.
 */

#include "gpio_input.h"

#include "esp_log.h"
#include "i2c_joystick.h"
#include "driver/gpio.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>

static const char *TAG = "GPIO_INPUT";

#define GPIO_MENU_NUM GPIO_NUM_41

static i2c_joystick_handle_t s_stick;

void gpio_input_init(void)
{
    // Allow the I2C joystick chip to boot on cold start before probing it.
    vTaskDelay(pdMS_TO_TICKS(500));

    esp_err_t ret = i2c_joystick_init(&s_stick);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize I2C joystick: %s", esp_err_to_name(ret));
        return;
    }
    vTaskDelay(pdMS_TO_TICKS(100));

    // GPIO41 built-in button: input with pull-up, active LOW.
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << GPIO_MENU_NUM),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));

    // Read a baseline so axes read neutral even if the first probe failed.
    hid_input_state_t baseline;
    memset(&baseline, 0, sizeof(baseline));
    baseline.joy1_x = 2048;
    baseline.joy1_y = 2048;
    baseline.joy2_x = 2048;
    baseline.joy2_y = 2048;
    gpio_input_read(&baseline);

    ESP_LOGI(TAG, "Input initialized");
}

void gpio_input_read(hid_input_state_t *s)
{
    if (s == NULL) {
        return;
    }

    memset(s, 0, sizeof(*s));

    // Default axes to center so a failed read leaves them neutral.
    s->joy1_x = 2048;
    s->joy1_y = 2048;
    s->joy2_x = 2048;
    s->joy2_y = 2048;

    if (s_stick.initialized) {
        esp_err_t ret = i2c_joystick_read_all(&s_stick,
                                               &s->joy1_x, &s->joy1_y,
                                               &s->btn_left, &s->btn_right,
                                               &s->btn_left_stick, &s->btn_right_stick);

        if (ret == ESP_OK) {
            ret = i2c_joystick_read_axis(&s_stick, JOY2_X_REG, &s->joy2_x);
        }
        if (ret == ESP_OK) {
            ret = i2c_joystick_read_axis(&s_stick, JOY2_Y_REG, &s->joy2_y);
        }
        if (ret != ESP_OK) {
            // Keep the neutral defaults on axis read failure.
        }
    }

    // GPIO41 built-in button is active LOW.
    s->gpio_pressed = (gpio_get_level(GPIO_MENU_NUM) == 0);
}
