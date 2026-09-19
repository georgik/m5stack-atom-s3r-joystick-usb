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
#include "iot_button.h"
#include "button_gpio.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>

static const char *TAG = "GPIO_INPUT";

#define GPIO_MENU_NUM GPIO_NUM_41

static i2c_joystick_handle_t s_stick;

/*
 * Software debounce for the four I2C joystick buttons. The StampFly chip
 * exposes raw, un-debounced button levels, so the mechanical contacts' bounce
 * on press/release can produce spurious edges. We require
 * BUTTON_DEBOUNCE_NEED consecutive identical reads before flipping the
 * reported state. This mirrors the ESP-IDF button driver's debounce that gates
 * the GPIO41 "menu" button below.
 */
#define BUTTON_DEBOUNCE_NEED 3

typedef struct {
    bool    stable;   // last reported (confirmed) button state
    uint8_t count;    // consecutive reads of the current level
} button_debouncer_t;

static button_debouncer_t s_btn_debounce[4];

static bool debounce_button(button_debouncer_t *b, bool raw)
{
    if (raw == b->stable) {
        if (b->count < 255) {
            b->count++;
        }
    } else {
        b->count = 1;
    }
    if (b->count >= BUTTON_DEBOUNCE_NEED) {
        b->stable = raw;
    }
    return b->stable;
}

/*
 * Debounced "menu" button (GPIO41). The ESP-IDF button driver reports a clean
 * press+release as a single click here; the main task then delivers it as a
 * one-frame pulse in hid_input_state_t::gpio_pressed.
 */
static button_handle_t s_menu_btn = NULL;
static volatile bool s_menu_btn_claim = false;
static portMUX_TYPE s_input_lock = SPINLOCK_INITIALIZER;

static void menu_button_cb(void *handle, void *usr)
{
    (void)handle;
    (void)usr;
    if (iot_button_get_event(handle) == BUTTON_SINGLE_CLICK) {
        s_menu_btn_claim = true;
    }
}

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

    // Built-in GPIO41 button: debounced by the ESP-IDF button driver. A clean
    // press+release ("single click") is surfaced via s_menu_btn_claim and
    // delivered as s->gpio_pressed in gpio_input_read(). This replaces the old
    // raw-level read + software debounce, so the profile menu no longer latches
    // the press-and-release that returned us here.
    const button_config_t btn_cfg = {
        .short_press_time = 20,   // ms debounce per edge
    };
    const button_gpio_config_t btn_gpio_cfg = {
        .gpio_num = GPIO_MENU_NUM,
        .active_level = 0,        // active LOW
    };
    ret = iot_button_new_gpio_device(&btn_cfg, &btn_gpio_cfg, &s_menu_btn);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create GPIO41 button: %s", esp_err_to_name(ret));
    } else {
        iot_button_register_cb(s_menu_btn, BUTTON_SINGLE_CLICK, NULL, menu_button_cb, NULL);
    }

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

    // Software-debounce the four I2C joystick buttons so contact bounce on
    // press/release does not produce spurious events. This mirrors the
    // ESP-IDF button driver's debounce that gates the GPIO41 "menu" button.
    s->btn_left        = debounce_button(&s_btn_debounce[BUTTON_LEFT],        s->btn_left);
    s->btn_right       = debounce_button(&s_btn_debounce[BUTTON_RIGHT],       s->btn_right);
    s->btn_left_stick  = debounce_button(&s_btn_debounce[BUTTON_LEFT_STICK],  s->btn_left_stick);
    s->btn_right_stick = debounce_button(&s_btn_debounce[BUTTON_RIGHT_STICK], s->btn_right_stick);

    // GPIO41 built-in button: deliver a single debounced "click" (press+release)
    // as a one-frame pulse via s->gpio_pressed. The first reader consumes it
    // (claim model), so returning to the profile menu does not re-latch the
    // press that triggered the return.
    if (s_menu_btn != NULL) {
        portENTER_CRITICAL(&s_input_lock);
        s->gpio_pressed = s_menu_btn_claim;
        s_menu_btn_claim = false;
        portEXIT_CRITICAL(&s_input_lock);
    } else {
        // Fallback: raw level (no debounce). The button driver is a required
        // dependency, so this should not happen.
        s->gpio_pressed = (gpio_get_level(GPIO_MENU_NUM) == 0);
    }
}
