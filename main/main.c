/**
 * @file main.c
 * @brief Raylib port of the AtomS3 USB Joystick firmware.
 *
 * Architecture:
 * - Display stack (GC9107 on SPI3_HOST) is driven directly via esp_lcd; rcore
 *   drives the framebuffer. The AtomS3R panel is a GC9107 (register-compatible
 *   with GC9A01) — the ST7789 driver sends Sitronix init commands the GC9107
 *   ignores, so we drive it with esp_lcd_gc9a01.
 * - The Raylib loop below drives the application state machine. Each frame it
 *   samples the I2C StampFly joystick + GPIO buttons, advances the active
 *   state, and draws the screen between BeginDrawing()/EndDrawing().
 * - HID report generation lives in hid_reports.c; MSC storage in
 *   msc_storage.c.
 *
 * Flow mirrors the reference gpio-keyboard.c app_main:
 *   profile menu -> [Mass Storage | Snake Game | BLE | USB HID]
 *
 * The display initialization / flush carries the validated fixes from the
 * mipidsi port (timing, MADTL 0x48, INVON, 130x129 framebuffer, no COG
 * offset). See wiki/display.md.
 */

#include "esp_log.h"
#include "esp_err.h"
#include "esp_system.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_gc9a01.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "raylib.h"

#include "hid_reports.h"
#include "ble_hid.h"
#include "gpio_input.h"
#include "profile_parser.h"
#include "profile_menu.h"
#include "snake_game.h"
#include "ui.h"
#include "msc_storage.h"
#include "backlight.h"

#include <string.h>

static const char *TAG = "M5STACK_ATOMS3R";

// Display handles from BSP
static esp_lcd_panel_handle_t g_panel = NULL;
static esp_lcd_panel_io_handle_t g_io = NULL;

/**
 * @brief Display flush callback for rcore
 *
 * raylib's SwapScreenBuffer vertically flips the framebuffer, so buf[i]
 * holds the natural-image row (h-1-i). We rebuild a natural-order DMA
 * buffer (row 0 = top of the scene) so the GC9107 writes it in order.
 * RGB565 is byte-swapped per pixel (__builtin_bswap16): little-endian
 * ESP32 -> big-endian SPI, matching mipidsi.
 *
 * The panel is physically 128x128 but we report 130x129 to raylib (see
 * display_get_dimensions()) and draw the window (x,y)-(x+w,y+h) with NO COG
 * offset. The +2px width covers the panel's spare right-edge pixels and the
 * +1px height removes the "unused pixels at the bottom" / alignment artifact.
 * See wiki/display.md §6.4 / §6.5.
 */
static void display_flush(const uint16_t *buf, uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
    if (!g_panel || !buf) {
        return;
    }

    // Draw the whole framebuffer naturally (top-first), in one call, exactly as
    // the validated reference firmware does. Splitting it into per-chunk
    // draw_bitmap calls made the GC9107 re-set RASET and wrap/repeat in the
    // lower third, because the last chunk's window end (y+129) exceeds the
    // 128-row panel. A single full-frame draw avoids that.
    //
    // release/v6.1 reserves a 32 KB internal DMA pool; the full 130x129
    // framebuffer (~33.5 KB) cannot fit in contiguous DMA-capable internal
    // SRAM, so heap_caps_malloc(MALLOC_CAP_DMA) fails with ESP_ERR_NO_MEM and
    // the SPI priv-TX-buffer path aborts. We therefore allocate the flush
    // framebuffer in PSRAM. With psram_dma_direct=1 on the panel IO (see
    // init_display), the SPI DMA path reads PSRAM directly and needs no
    // internal scratch buffer at all, so the DMA error disappears. Fall back
    // to plain SRAM only if PSRAM is unavailable.
    const size_t size = (size_t)h * w * sizeof(uint16_t);
    uint16_t *fb = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
    if (!fb) {
        fb = malloc(size);   // plain SRAM fallback
    }
    if (!fb) {
        ESP_LOGE(TAG, "Failed to allocate flush buffer");
        return;
    }

    // Rebuild a natural-order framebuffer: undo raylib's vertical flip so that
    // fb[r] is the r-th row of the drawn scene (r = 0 is the top).
    for (uint16_t r = 0; r < h; r++) {
        uint16_t src_row = (uint16_t)(h - 1 - r);
        const uint16_t *sp = buf + ((uint32_t)src_row * w);
        uint16_t *dp = fb + ((uint32_t)r * w);
        for (uint16_t c = 0; c < w; c++) {
            dp[c] = __builtin_bswap16(sp[c]);   // little-endian ESP32 -> big-endian SPI LCD
        }
    }

    // Draw the full framebuffer directly, with no COG offset (wiki/display.md §6.4).
    esp_err_t ret = esp_lcd_panel_draw_bitmap(g_panel, x, y, x + w, y + h, fb);
    heap_caps_free(fb);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to draw bitmap: %s", esp_err_to_name(ret));
    }
}

/**
 * @brief Get display dimensions callback.
 *
 * The panel is physically 128x128; we report 130x129 so the flush can draw a
 * framebuffer 2px wider / 1px taller than the panel (wiki/display.md §6.4).
 */
static void display_get_dimensions(uint16_t *w, uint16_t *h)
{
    if (w) *w = 130;
    if (h) *h = 129;
}

/*
 * Screen rotation state. When s_screen_inverted is true the main screen is
 * rotated 180 degrees so the AtomS3R can be played upside-down (USB connector
 * pointing toward the player) — see wiki/handover.md. The rotation is done in
 * the LCD controller via esp_lcd_panel_mirror(): the board's GC9107 is
 * hardware-mirrored on X (MX, set at init), so a 180-degree rotation is MX +
 * MY. The natural-order framebuffer produced by display_flush() is unchanged;
 * the panel just writes it flipped on both axes.
 */
static bool s_screen_inverted = false;

/*
 * Brief backlight pulse so the user gets tactile confirmation of a rotation
 * toggle. The panel boots at full brightness; a quick dim+restore is enough to
 * notice without changing the steady-state brightness.
 */
static void flip_feedback(void)
{
    backlight_set_brightness(20);
    vTaskDelay(pdMS_TO_TICKS(80));
    backlight_on();
}

/*
 * Toggle (or set) the 180-degree screen rotation. Called from the profile menu
 * when the LEFT face button ("L") is pressed.
 */
static void set_screen_inverted(bool inverted)
{
    s_screen_inverted = inverted;
    if (g_panel != NULL) {
        /* The panel boots with MX set (board hardware mirror, MY clear) so it
         * reads correctly in the normal (USB-down) orientation. When the board
         * is rotated 180 degrees in-plane (USB toward the player) the display
         * state must be the COMPLEMENT of that baseline: MX clear, MY set.
         *
         *   inverted=false -> MX=true,  MY=false -> normal (matches init)
         *   inverted=true  -> MX=false, MY=true  -> 180 degrees, reads upright
         *
         * (Doing MX=true, MY=true as before leaves a leftover horizontal
         *  mirror, i.e. backward letters — that is what we observed.) */
        ESP_ERROR_CHECK(esp_lcd_panel_mirror(g_panel, !inverted, inverted));
    }
    ESP_LOGI(TAG, "Screen rotation: %s",
             inverted ? "180 degrees (inverted)" : "normal");
}

/*
 * Apply the 180-degree DEVICE rotation to a sampled input snapshot.
 *
 * When the main screen is rotated 180 degrees (s_screen_inverted) the board is
 * played upside-down (USB connector toward the player). From the player's
 * viewpoint this means:
 *   - the physical LEFT stick now sits on the RIGHT (and the physical right on
 *     the left)  ->  swap the two sticks;
 *   - pushing a stick "up" now moves the cursor the other way on the rotated
 *     screen  ->  invert both axes around the 2048 centre.
 *
 * It is applied to the sampled input BEFORE the state machine consumes it, so
 * the profile menu, every HID/gamepad report and the snake game all see a
 * natural, right-side-up layout while the display is flipped. With
 * s_screen_inverted == false this is a no-op, so the normal orientation is
 * untouched.
 */
static void apply_screen_flip(hid_input_state_t *s)
{
    if (!s_screen_inverted || s == NULL) {
        return;
    }
    const int cx = 2048;  // stick centre
    // Invert each axis around centre: 2*cx - raw  (== 4096 - raw).
    int16_t j1x = (int16_t)(2 * cx - s->joy1_x);
    int16_t j1y = (int16_t)(2 * cx - s->joy1_y);
    int16_t j2x = (int16_t)(2 * cx - s->joy2_x);
    int16_t j2y = (int16_t)(2 * cx - s->joy2_y);
    // Swap the sticks: physical left (joy1) -> right output, and vice versa.
    s->joy1_x = (uint16_t)j2x;
    s->joy1_y = (uint16_t)j2y;
    s->joy2_x = (uint16_t)j1x;
    s->joy2_y = (uint16_t)j1y;
}
extern void raylib_esp_set_display_callbacks(
    void (*flush_fn)(const uint16_t *buf, uint16_t x, uint16_t y, uint16_t w, uint16_t h),
    void (*get_dim_fn)(uint16_t *w, uint16_t *h)
);

#define RAYLIB_TASK_STACK_SIZE (128 * 1024)

/**
 * @brief Initialize the GC9107 display (SPI3_HOST) + LP5562 backlight.
 *
 * Timing / settings mirror the working Rust mipidsi firmware (see
 * wiki/display.md §6.2 / §6.4.1): 500ms power-on settle BEFORE reset, 200ms
 * AFTER reset, MADTL 0x48 (mirror(true,false)), INVON colour.
 */
static esp_err_t init_display(void)
{
    ESP_LOGI(TAG, "Initializing display...");

    // AtomS3R display is a GC9107 (register-compatible with GC9A01). The ST7789
    // driver sends Sitronix init commands the GC9107 ignores, so we use
    // esp_lcd_gc9a01. Pinout from M5Stack AtomS3R hardware docs.
    #define M5STACK_ATOM_S3R_LCD_MOSI      GPIO_NUM_21
    #define M5STACK_ATOM_S3R_LCD_SCLK      GPIO_NUM_15
    #define M5STACK_ATOM_S3R_LCD_CS        GPIO_NUM_14
    #define M5STACK_ATOM_S3R_LCD_DC        GPIO_NUM_42
    #define M5STACK_ATOM_S3R_LCD_RST       GPIO_NUM_48

    // SPI bus configuration (ESP-IDF 6 API)
    spi_bus_config_t bus_cfg = {
        .sclk_io_num = M5STACK_ATOM_S3R_LCD_SCLK,
        .mosi_io_num = M5STACK_ATOM_S3R_LCD_MOSI,
        .miso_io_num = GPIO_NUM_NC,
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        .max_transfer_sz = 128 * 128 * sizeof(uint16_t),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &bus_cfg, SPI_DMA_CH_AUTO));

    // LCD IO SPI configuration (ESP-IDF 6 API)
    esp_lcd_panel_io_spi_config_t io_cfg = {
        .cs_gpio_num = M5STACK_ATOM_S3R_LCD_CS,
        .dc_gpio_num = M5STACK_ATOM_S3R_LCD_DC,
        .spi_mode = 0,
        .pclk_hz = 40 * 1000 * 1000,
        .trans_queue_depth = 10,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .flags = {
            .dc_low_on_data = 0,
            // release/v6.1: read the color (draw_bitmap) buffer directly from
            // PSRAM instead of staging it into a small internal DMA buffer.
            // This is what makes the PSRAM-allocated flush framebuffer usable
            // and avoids the "Failed to allocate priv TX buffer" failure.
            .psram_dma_direct = 1,
        },
    };

    // Create panel IO (ESP-IDF 6 API - uses SPI3_HOST directly)
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI3_HOST, &io_cfg, &g_io));

    // GC9107 panel configuration
    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = M5STACK_ATOM_S3R_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
        .bits_per_pixel = 16,
    };

    // Initialize GC9107 panel via the GC9A01 driver
    ESP_ERROR_CHECK(esp_lcd_new_panel_gc9a01(g_io, &panel_cfg, &g_panel));

    // Fix: 500ms power-on settle BEFORE reset/init (mipidsi behaviour).
    // Without this, MADTL/init commands get lost -> split / low-res display.
    vTaskDelay(pdMS_TO_TICKS(500));

    ESP_ERROR_CHECK(esp_lcd_panel_reset(g_panel));

    // Fix: 200ms settle AFTER reset, BEFORE init command seq.
    // Sending init commands too soon after reset drops them -> bad resolution.
    vTaskDelay(pdMS_TO_TICKS(200));

    ESP_ERROR_CHECK(esp_lcd_panel_init(g_panel));

    // MADTL 0x48: this board's GC9107 is hardware horizontally mirrored, so the
    // MX column-flip bit is required to produce a non-mirrored image.
    // (The MY bit tried earlier only flips rows and did not fix the mirror.)
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(g_panel, true, false));

    // INVON colour: the panel boots inverted and the GC9107 vendor table
    // reinforces it, so INVOFF cannot clear it — use INVON (wiki/display.md §6.6).
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(g_panel, true));

    // Turn on display
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(g_panel, true));

    // Small delay for display to stabilize
    vTaskDelay(pdMS_TO_TICKS(100));

    // AtomS3R backlight: the white-backlight LEDs are powered by an LP5562
    // constant-current LED driver on I2C (addr 0x30, SDA=GPIO45, SCL=GPIO0).
    // The working reference (rust-m5stack-atom-s3r) enables it explicitly;
    // without this the panel is dark even though the LCD controller above is
    // initialised. This is the primary cause of "screen not initialized".
    ESP_ERROR_CHECK(backlight_init());

    // Register display callbacks with rcore
    raylib_esp_set_display_callbacks(display_flush, display_get_dimensions);

    uint16_t w, h;
    display_get_dimensions(&w, &h);
    ESP_LOGI(TAG, "Display initialized: %dx%d", w, h);
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/* Application state machine                                           */
/* ------------------------------------------------------------------ */

/* Poll rates (match reference). */
#define JOYSTICK_POLL_MS 20
#define BLE_POLL_MS      50

/* Joystick thresholds for menu navigation. */
#define JOYSTICK_THRESHOLD_LOW      800
#define JOYSTICK_THRESHOLD_HIGH     3200

typedef enum {
    APP_STATE_MENU,
    APP_STATE_USB_HID,
    APP_STATE_BLE_HID,
    APP_STATE_MSC,
    APP_STATE_SNAKE,
    APP_STATE_ERROR,
} app_state_t;

/*
 * On-screen error state. USB/BLE/MSC modes cannot be debugged via serial once
 * active (the USB port is busy with HID, BLE has no serial), so mode-init
 * failures are shown on the display instead of only being logged. The user
 * acknowledges the error by pressing any button, which returns to the menu.
 */
static char s_error_title[32] = "";
static char s_error_msg[48] = "";

static void set_error(const char *title, const char *msg)
{
    strncpy(s_error_title, title, sizeof(s_error_title) - 1);
    s_error_title[sizeof(s_error_title) - 1] = '\0';
    strncpy(s_error_msg, msg, sizeof(s_error_msg) - 1);
    s_error_msg[sizeof(s_error_msg) - 1] = '\0';
    ESP_LOGE(TAG, "Error: %s %s", s_error_title, s_error_msg);
}

/* Sampled hardware input, filled by gpio_input_read(). */
static bool any_button_pressed(const hid_input_state_t *s)
{
    return (s->btn_left || s->btn_right || s->btn_left_stick ||
            s->btn_right_stick || s->gpio_pressed);
}

/*
 * GPIO41 "menu" button. The ESP-IDF button driver debounces the press+release
 * "single click" and surfaces it as a one-frame pulse in s->gpio_pressed
 * (see gpio_input_read()). Return true while that pulse is present; the first
 * handler to observe it consumes the claim, so returning to the profile menu
 * does not re-latch the same press. GPIO41 is active-LOW.
 */
static bool menu_button_pressed(const hid_input_state_t *s)
{
    return s->gpio_pressed;
}

/* Choose the next state from the selected profile (no side effects). */
static app_state_t app_state_for_profile(const profile_info_t *p)
{
    if (p == NULL) {
        return APP_STATE_MENU;
    }
    if (strcmp(p->name, "Mass Storage") == 0) {
        return APP_STATE_MSC;
    }
    if (strcmp(p->name, "Snake Game") == 0) {
        return APP_STATE_SNAKE;
    }
    if (p->connection_mode == CONNECTION_MODE_BLE) {
        return APP_STATE_BLE_HID;
    }
    return APP_STATE_USB_HID;
}

/* One-shot "wait for buttons released" guard shared by active modes. */
static bool mode_entry_wait(bool *wait, uint32_t *wait_ms, const hid_input_state_t *s)
{
    if (!*wait) {
        // On entry, if a button is still held (e.g. from the menu selection),
        // wait until it is released so a stale press is not acted on.
        if (any_button_pressed(s)) {
            *wait = true;
            *wait_ms = xTaskGetTickCount();
            return true;  // stay in entry-wait until the button is released
        }
        return false;  // nothing held -> proceed immediately
    }
    if (!any_button_pressed(s) || (xTaskGetTickCount() - *wait_ms) > 1000) {
        *wait = false;
    }
    return *wait;
}

static void handle_menu(const hid_input_state_t *s, app_state_t *state,
                        const profile_info_t **profile)
{
    // One-shot entry guard. Armed whenever we (re)enter the menu so the first
    // frames after returning from a mode are protected against a held button
    // re-latching as a fresh selection.
    static bool s_menu_entry_done = false;
    static bool s_menu_entry_wait = false;
    static uint32_t s_menu_entry_ms = 0;

    // Activate the menu on first entry (profiles + state). Until this runs
    // profile_menu_render() early-returns (s_menu_active == false), so nothing
    // is drawn.
    if (!profile_menu_is_active()) {
        profile_menu_show();
        // (Re)arm the entry guard for this menu visit.
        s_menu_entry_done = false;
        s_menu_entry_wait = false;
    }

    if (profile_menu_is_active()) {
        bool joy1_up = (s->joy1_y < JOYSTICK_THRESHOLD_LOW);
        bool joy1_down = (s->joy1_y > JOYSTICK_THRESHOLD_HIGH);

        // Entry-wait guard: on the first frames after returning from a mode a
        // button may still be held (e.g. btn_right used to leave the game, or
        // the GPIO41 screen button). Wait until it is released so the menu does
        // not treat that press as a fresh selection. One-shot: once cleared,
        // fresh presses made inside the menu are acted on immediately.
        if (!s_menu_entry_done && mode_entry_wait(&s_menu_entry_wait, &s_menu_entry_ms, s)) {
            profile_menu_render();
            return;
        }
        s_menu_entry_done = true;

        /*
         * Screen-flip toggle: the LEFT face button ("L") rotates the main
         * screen 180 degrees. Edge-detected on RELEASE (button-up) so a single
         * press+release toggles exactly once. We trigger on button-up rather
         * than the press edge, so no frame-rate or debounce speeding-up is
         * required — we simply wait for the user to let go, at which point the
         * RAW register pattern has returned to all 1s. This button is kept OUT
         * of the "confirm selection" set computed just below, so pressing it
         * never also confirms a profile and leaves the menu.
         */
        static bool s_btn_left_last = false;
        if (!s->btn_left && s_btn_left_last) {
            set_screen_inverted(!s_screen_inverted);
            flip_feedback();
        }
        s_btn_left_last = s->btn_left;

        bool button_pressed = (s->btn_right || s->btn_left_stick ||
                               s->btn_right_stick || s->gpio_pressed);

        if (profile_menu_handle_input(joy1_up, joy1_down, button_pressed)) {
            *profile = profile_menu_get_selected_info();
            *state = app_state_for_profile(*profile);
            profile_menu_hide();
            return;
        }

        // Keep rendering while the menu stays open.
        profile_menu_render();
    }
}

static void handle_usb_hid(const hid_input_state_t *s, app_state_t *state,
                           const profile_info_t *profile)
{
    static bool s_setup = false;
    static bool s_entry_done = false;
    static bool s_entry_wait = false;
    static uint32_t s_entry_ms = 0;

    if (!s_setup) {
        hid_reports_setup(profile);

        if (hid_reports_install_usb_hid() != ESP_OK) {
            ESP_LOGE(TAG, "tinyusb HID install failed");
            set_error("USB HID", "install failed");
            s_setup = false;
            *state = APP_STATE_ERROR;
            return;
        }
        ui_reset_uptime();
        s_setup = true;
        s_entry_done = false;
    }

    // Entry-wait guard: only honored on the first frames after start, to let a
    // button held from the menu selection settle. Once cleared it stays cleared,
    // so button presses during operation reach the handlers below.
    if (!s_entry_done && mode_entry_wait(&s_entry_wait, &s_entry_ms, s)) {
        // Draw while a button is still held after selection.
        ui_show_usb_active(profile ? profile->name : "Gamepad");
        return;
    }
    s_entry_done = true;

    ui_show_usb_active(profile ? profile->name : "Gamepad");

    if (menu_button_pressed(s)) {
        ESP_LOGI(TAG, "Menu button pressed - restarting");
        esp_restart();
    }

    if (hid_reports_usb_mounted()) {
        hid_reports_send_gamepad(s);
        hid_reports_send_keyboard(s);
        hid_reports_send_mouse(s);
    }
}

static void send_ble_reports(const hid_input_state_t *s, const profile_info_t *profile)
{
    profile_type_t type = (profile ? profile->type : PROFILE_TYPE_GAMEPAD);
    if (type == PROFILE_TYPE_KEYBOARD_MOUSE) {
        hid_reports_send_keyboard_ble(s);
        hid_reports_send_mouse_ble(s);
    } else if (type == PROFILE_TYPE_KEYBOARD) {
        hid_reports_send_keyboard_ble(s);
    } else {
        hid_reports_send_gamepad_ble(s);
    }
}

static void handle_ble_hid(const hid_input_state_t *s, app_state_t *state,
                           const profile_info_t *profile)
{
    static bool s_init = false;
    static bool s_connected = false;
    static uint32_t s_send_ready_ms = 0;
    static bool s_entry_done = false;
    static bool s_entry_wait = false;
    static uint32_t s_entry_ms = 0;

    if (!s_init) {
        if (ble_hid_init() != ESP_OK) {
            ESP_LOGE(TAG, "ble_hid_init failed");
            set_error("BLE HID", "init failed");
            s_init = false;
            *state = APP_STATE_ERROR;
            return;
        }
        ui_reset_uptime();
        s_init = true;
        s_entry_done = false;
    }

    // Entry-wait guard: only honored on the first frames after start, to let a
    // button held from the menu selection settle. Once cleared it stays cleared,
    // so button presses during operation reach the handlers below.
    if (!s_entry_done && mode_entry_wait(&s_entry_wait, &s_entry_ms, s)) {
        ui_show_usb_active(profile ? profile->name : "BLE HID");
        return;
    }
    s_entry_done = true;

    bool is_connected = ble_hid_is_connected();
    if (is_connected && !s_connected) {
        s_connected = true;
        // Wait ~2s after connection for HID service discovery.
        s_send_ready_ms = xTaskGetTickCount() + pdMS_TO_TICKS(2000);
        ESP_LOGI(TAG, "Connected, waiting 2s for HID service discovery");
    }

    ui_show_usb_active(profile ? profile->name : "BLE HID");

    if (menu_button_pressed(s)) {
        ble_hid_deinit();
        ESP_LOGI(TAG, "Menu button pressed - restarting");
        esp_restart();
    }

    if (is_connected && (xTaskGetTickCount() >= s_send_ready_ms)) {
        send_ble_reports(s, profile);
    }
}

static void handle_msc(const hid_input_state_t *s, app_state_t *state)
{
    static bool s_started = false;
    static bool s_entry_done = false;
    static bool s_entry_wait = false;
    static uint32_t s_entry_ms = 0;

    if (!s_started) {
        if (hid_reports_install_usb_msc() != ESP_OK) {
            ESP_LOGE(TAG, "tinyusb MSC install failed");
            set_error("USB MSC", "install failed");
            s_started = false;
            *state = APP_STATE_ERROR;
            return;
        }
        if (msc_storage_start_usb_mode() != ESP_OK) {
            ESP_LOGE(TAG, "MSC start failed");
            set_error("Storage", "mount failed");
            s_started = false;
            *state = APP_STATE_ERROR;
            return;
        }
        s_started = true;
        s_entry_done = false;
    }

    // Entry-wait guard: only honored on the first frames after start, to let a
    // button held from the menu selection settle. Once cleared it stays cleared,
    // so button presses during operation reach the handlers below.
    if (!s_entry_done && mode_entry_wait(&s_entry_wait, &s_entry_ms, s)) {
        ui_show_usb_active("Mass Storage");
        return;
    }
    s_entry_done = true;

    ui_show_usb_active("Mass Storage");

    // Leave Mass Storage mode. The tinyUSB driver is shared between the USB HID
    // modes and MSC, and tinyusb_driver_install() only succeeds once per boot,
    // so we reboot to reset it before any mode can be re-selected. This mirrors
    // the USB HID exit path (menu button -> esp_restart) and the reference
    // firmware, which runs one profile per boot. An eject is detected
    // separately because tud_mounted() stays true after the host unmounts the
    // volume.
    if (menu_button_pressed(s) || !hid_reports_usb_mounted() || msc_storage_check_ejected()) {
        msc_storage_stop_usb_mode();
        ESP_LOGI(TAG, "Exiting Mass Storage mode - rebooting to return to menu");
        esp_restart();
    }
}

static void handle_snake(const hid_input_state_t *s, app_state_t *state)
{
    static bool s_started = false;
    static bool s_entry_done = false;
    static bool s_entry_wait = false;
    static uint32_t s_entry_ms = 0;

    // Edge-detected "return to menu" for the face buttons handled here (a held
    // button triggers exactly once). btn_left is passed to snake_game_handle_input()
    // as button_b below.
    static bool s_btn_right_last = false;
    static bool s_btn_rstick_last = false;
    bool ret_to_menu = (s->btn_right && !s_btn_right_last)
                    || (s->btn_right_stick && !s_btn_rstick_last);
    s_btn_right_last = s->btn_right;
    s_btn_rstick_last = s->btn_right_stick;

    if (!s_started) {
        snake_game_start();
        s_started = true;
        s_entry_done = false;
    }

    // Entry-wait guard: only honored on the first frames after start, to let a
    // button held from the menu selection settle. Once cleared it stays cleared,
    // so button presses during play reach the game input handler below.
    if (!s_entry_done && mode_entry_wait(&s_entry_wait, &s_entry_ms, s)) {
        snake_game_render();
        return;
    }
    s_entry_done = true;

    // Leave the game and return to the profile menu from the GPIO41 screen
    // button or btn_right / btn_right_stick, in every state (playing or game
    // over). btn_left is passed through to snake_game_handle_input() as button_b.
    if (ret_to_menu || menu_button_pressed(s)) {
        ESP_LOGI(TAG, "Return button pressed - returning to profile menu");
        *state = APP_STATE_MENU;
        s_started = false;
        return;
    }

    bool joy1_up = (s->joy1_y < 1000);
    bool joy1_down = (s->joy1_y > 3000);
    bool joy1_left = (s->joy1_x < 1000);
    bool joy1_right = (s->joy1_x > 3000);
    bool button_a = s->btn_left_stick;   // joystick click: restart / return-to-menu
    bool button_b = s->btn_left;         // face button: return-to-menu

    bool exit_game = snake_game_handle_input(joy1_up, joy1_down, joy1_left, joy1_right,
                                             button_a, button_b);
    if (exit_game) {
        *state = APP_STATE_MENU;
        s_started = false;
        return;
    }

    snake_game_update();
    snake_game_render();
}

/*
 * On-screen error state: draw the failure and wait for the user to acknowledge
 * it (press any button) before returning to the profile menu. This is the only
 * way to surface a mode-init failure while USB/BLE/MSC is active and serial is
 * unavailable.
 */
static void handle_error(const hid_input_state_t *s, app_state_t *state)
{
    if (any_button_pressed(s)) {
        *state = APP_STATE_MENU;
        return;
    }
    ui_show_error(s_error_title, s_error_msg);
}

void raylib_task(void *pvParameter)
{
    ESP_LOGI(TAG, "Initializing Raylib...");

    uint16_t w, h;
    display_get_dimensions(&w, &h);
    InitWindow(w, h, "M5Stack-AtomS3R Raylib");

    // Initialize hardware input (I2C joystick + GPIO41 button).
    gpio_input_init();

    // Initialize snake game state.
    snake_game_init();

    app_state_t state = APP_STATE_MENU;
    const profile_info_t *profile = NULL;

    while (!WindowShouldClose()) {
        BeginDrawing();
        ClearBackground(BLACK);

        hid_input_state_t input;
        gpio_input_read(&input);

        // Rotate the sampled input with the display: when the board is flipped
        // 180 degrees the sticks swap left<->right and their axes invert, so
        // the menu, HID reports and snake all feel natural upside-down.
        apply_screen_flip(&input);

        switch (state) {
        case APP_STATE_MENU:
            handle_menu(&input, &state, &profile);
            break;
        case APP_STATE_USB_HID:
            handle_usb_hid(&input, &state, profile);
            break;
        case APP_STATE_BLE_HID:
            handle_ble_hid(&input, &state, profile);
            break;
        case APP_STATE_MSC:
            handle_msc(&input, &state);
            break;
        case APP_STATE_SNAKE:
            handle_snake(&input, &state);
            break;
        case APP_STATE_ERROR:
            handle_error(&input, &state);
            break;
        }

        EndDrawing();

        // Frame pacing depends on the active mode.
        switch (state) {
        case APP_STATE_USB_HID:
            vTaskDelay(pdMS_TO_TICKS(JOYSTICK_POLL_MS));
            break;
        case APP_STATE_BLE_HID:
            vTaskDelay(pdMS_TO_TICKS(BLE_POLL_MS));
            break;
        default:
            vTaskDelay(pdMS_TO_TICKS(40));
            break;
        }
    }

    CloseWindow();
    vTaskDelete(NULL);
}

void app_main(void)
{
    ESP_LOGI(TAG, "Starting AtomS3 USB Joystick");

    ESP_ERROR_CHECK(init_display());

    // Default to the un-rotated (normal) orientation on boot so the device
    // does not start flipped. The user toggles 180 degrees on the fly with the
    // L face button (see set_screen_inverted / handle_menu).
    set_screen_inverted(false);

    // Initialize MSC storage (FATfs) so the 'storage' partition is mounted.
    // This is the only function that mounts /storage, so the profile menu can
    // read /storage/profiles/ (BLE profiles) and Mass Storage mode can work.
    // Mirrors the reference gpio-keyboard.c app_main (which calls it before
    // showing the profile menu). Without this the FAT is never mounted and the
    // profile parser silently falls back to built-in defaults.
    esp_err_t err = msc_storage_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to initialize MSC storage: %s", esp_err_to_name(err));
        ESP_LOGW(TAG, "Continuing without mass storage support");
    } else {
        ESP_LOGI(TAG, "MSC storage initialized: %s", msc_storage_get_mount_point());
    }

    xTaskCreatePinnedToCore(raylib_task, "raylib", RAYLIB_TASK_STACK_SIZE,
                            NULL, 5, NULL, 1);
}
