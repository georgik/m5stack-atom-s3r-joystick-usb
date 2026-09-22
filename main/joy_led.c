/*
 * WS2812 RGB LED control for the Atom JoyStick sub-board (see joy_led.h).
 *
 * The LEDs are on ESP32 GPIO6, driven with the RMT peripheral via the
 * esp-led_strip component. This is a plain one-wire WS2812 stream.
 */

#include "joy_led.h"

#include "esp_log.h"
#include "led_strip.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <math.h>

#define JOY_LED_GPIO          6      /* AtomS3 GPIO6 -> WS2812C DIN on base   */
#define JOY_LED_NUM_LEDS      2      /* one near the L button, one near R      */
#define JOY_LED_BRIGHTNESS    0.05f    /* scale full-brightness output to 5%     */

static const char *TAG = "joy_led";
static led_strip_handle_t s_strip = NULL;

/*
 * Convert HSV (H in degrees 0-360, S/V in 0-1) to 8-bit RGB. Small, self
 * contained helper so the LED module stays independent of the UI framework.
 */
static void hsv_to_rgb(double h, double s, double v,
                       uint8_t *r, uint8_t *g, uint8_t *b)
{
    const double c = v * s;
    const double x = c * (1.0 - fabs(fmod(h / 60.0, 2.0) - 1.0));
    const double m = v - c;
    double rp, gp, bp;

    if      (h < 60.0) { rp = c; gp = x; bp = 0.0; }
    else if (h < 120.0) { rp = x; gp = c; bp = 0.0; }
    else if (h < 180.0) { rp = 0.0; gp = c; bp = x; }
    else if (h < 240.0) { rp = 0.0; gp = x; bp = c; }
    else if (h < 300.0) { rp = x; gp = 0.0; bp = c; }
    else                { rp = c; gp = 0.0; bp = x; }

    *r = (uint8_t)lround((rp + m) * 255.0);
    *g = (uint8_t)lround((gp + m) * 255.0);
    *b = (uint8_t)lround((bp + m) * 255.0);
}

void joy_led_init(void)
{
    if (s_strip != NULL) {
        return;
    }

    esp_err_t ret = led_strip_new_rmt_device(
        &(led_strip_config_t){
            .strip_gpio_num         = JOY_LED_GPIO,
            .max_leds               = JOY_LED_NUM_LEDS,
            .led_model              = LED_MODEL_WS2812,
            .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        },
        &(led_strip_rmt_config_t){
            .clk_src         = RMT_CLK_SRC_DEFAULT,
            .resolution_hz   = 10 * 1000 * 1000, /* 10 MHz */
            .flags           = { .with_dma = false, },
        },
        &s_strip);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to create led strip on GPIO%d: %s",
                 JOY_LED_GPIO, esp_err_to_name(ret));
        return;
    }

    led_strip_clear(s_strip);
    ESP_LOGI(TAG, "WS2812 strip ready on GPIO%d (%d leds)", JOY_LED_GPIO, JOY_LED_NUM_LEDS);
}

void joy_led_set(uint8_t r, uint8_t g, uint8_t b)
{
    if (s_strip == NULL) {
        return;
    }
    for (int i = 0; i < JOY_LED_NUM_LEDS; i++) {
        led_strip_set_pixel(s_strip, i, r, g, b);
    }
    led_strip_refresh(s_strip);
}

void joy_led_clear(void)
{
    if (s_strip == NULL) {
        return;
    }
    led_strip_clear(s_strip);
}

void joy_led_update_rainbow(void)
{
    if (s_strip == NULL) {
        return;
    }

    /* Sweep the full hue circle over ~2 seconds, driven by the FreeRTOS tick
     * counter (a real, monotonic time source on the ESP32) instead of raylib's
     * GetTime(), which does not advance reliably in this build. fmod keeps the
     * sweep phase-locked to wall time regardless of how often we are called. */
    const uint32_t ticks_per_rev = (uint32_t) configTICK_RATE_HZ * 2; /* 2 s */
    const double phase = ((xTaskGetTickCount() % ticks_per_rev) /
                          (double) ticks_per_rev);
    const double h = phase * 360.0;

    uint8_t r, g, b;
    hsv_to_rgb(h, 1.0, JOY_LED_BRIGHTNESS, &r, &g, &b);

    for (int i = 0; i < JOY_LED_NUM_LEDS; i++) {
        led_strip_set_pixel(s_strip, i, r, g, b);
    }
    led_strip_refresh(s_strip);
}

/**
 * @brief Smoothly fade the base LEDs up to JOY_LED_BRIGHTNESS (a "glow").
 *
 * Linear ramp from off to the target brightness over ~0.5 s, used as the boot
 * indicator. Blocks via vTaskDelay, so call it from app_main before the UI.
 */
void joy_led_glow(void)
{
    if (s_strip == NULL) {
        return;
    }

    const uint8_t target = (uint8_t)lround(JOY_LED_BRIGHTNESS * 255.0);
    const int steps = 20;
    const TickType_t step_delay = pdMS_TO_TICKS(25); /* ~0.5 s total */

    for (int step = 0; step <= steps; step++) {
        uint8_t v = (uint8_t)((step * (int)target) / steps);
        for (int i = 0; i < JOY_LED_NUM_LEDS; i++) {
            led_strip_set_pixel(s_strip, i, v, v, v); /* white glow */
        }
        led_strip_refresh(s_strip);
        vTaskDelay(step_delay);
    }
}
