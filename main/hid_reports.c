/*
 * HID report generation for AtomS3 USB Joystick (Raylib port).
 *
 * Ported from the reference firmware gpio-keyboard.c report-generation section
 * into an isolated module. It owns the TinyUSB descriptors + current descriptor
 * pointers, the USB + BLE report builders (gamepad / keyboard / mouse), the
 * TinyUSB device-event handler, and the TinyUSB callbacks.
 */

#include "hid_reports.h"

#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "class/hid/hid_device.h"

#include "ble_hid.h"

static const char *TAG = "HID_REPORTS";

/* USB report IDs */
enum {
    REPORT_ID_GAMEPAD = 1,
    REPORT_ID_KEYBOARD = 2,
    REPORT_ID_MOUSE = 3,
    REPORT_ID_COUNT
};

/* Descriptor total lengths */
#define CONFIG_TOTAL_LEN_GAMEPAD   (TUD_CONFIG_DESC_LEN + TUD_HID_INOUT_DESC_LEN)
#define CONFIG_TOTAL_LEN_MSC       (TUD_CONFIG_DESC_LEN + TUD_MSC_DESC_LEN)
#define CONFIG_TOTAL_LEN_KEYBOARD  (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN)
#define CONFIG_TOTAL_LEN_COMBO     (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN)

/* ---- USB HID: Gamepad descriptors ---- */
static const uint8_t hid_report_descriptor_gamepad[] = {
    TUD_HID_REPORT_DESC_GAMEPAD(HID_REPORT_ID(REPORT_ID_GAMEPAD))
};

static const uint8_t hid_configuration_descriptor_gamepad[] = {
    TUD_CONFIG_DESCRIPTOR(1, 1, 0, CONFIG_TOTAL_LEN_GAMEPAD,
                          TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    TUD_HID_INOUT_DESCRIPTOR(0, 4, HID_ITF_PROTOCOL_NONE,
                             sizeof(hid_report_descriptor_gamepad),
                             0x81, 0x01, 64, 10)
};

/* ---- USB HID: Keyboard-only descriptors ---- */
static const uint8_t hid_report_descriptor_keyboard[] = {
    TUD_HID_REPORT_DESC_KEYBOARD(HID_REPORT_ID(HID_ITF_PROTOCOL_KEYBOARD))
};

static const uint8_t hid_configuration_descriptor_keyboard[] = {
    TUD_CONFIG_DESCRIPTOR(1, 1, 0, CONFIG_TOTAL_LEN_KEYBOARD,
                          TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    TUD_HID_DESCRIPTOR(0, 4, false, sizeof(hid_report_descriptor_keyboard),
                       0x81, 16, 10)
};

/* ---- USB HID: Composite keyboard + mouse descriptors ---- */
static const uint8_t hid_report_descriptor_combo[] = {
    TUD_HID_REPORT_DESC_KEYBOARD(HID_REPORT_ID(REPORT_ID_KEYBOARD)),
    TUD_HID_REPORT_DESC_MOUSE(HID_REPORT_ID(REPORT_ID_MOUSE))
};

static const uint8_t hid_configuration_descriptor_combo[] = {
    TUD_CONFIG_DESCRIPTOR(1, 1, 0, CONFIG_TOTAL_LEN_COMBO,
                          TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    TUD_HID_DESCRIPTOR(0, 0, false, sizeof(hid_report_descriptor_combo),
                       0x81, 16, 10)
};

/* ---- USB MSC descriptors ---- */
enum { ITF_NUM_MSC = 0, ITF_NUM_MSC_TOTAL };
enum { EDPT_MSC_OUT = 0x01, EDPT_MSC_IN = 0x81 };

static const uint8_t msc_configuration_descriptor[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_MSC_TOTAL, 0, CONFIG_TOTAL_LEN_MSC,
                          TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    TUD_MSC_DESCRIPTOR(ITF_NUM_MSC, 0, EDPT_MSC_OUT, EDPT_MSC_IN, 64)
};

/* String descriptor (language + manufacturer/product/serial/StampFly) */
static const char *hid_string_descriptor[5] = {
    (char[]){0x09, 0x04},
    "M5Stack",
    "M5Stack AtomS3",
    "123456",
    "StampFly Controller"
};

/* Current (profile-selected) descriptor pointers + lengths */
static const uint8_t *current_hid_report_descriptor = hid_report_descriptor_gamepad;
static size_t current_hid_report_desc_len = sizeof(hid_report_descriptor_gamepad);
static const uint8_t *current_hid_config_descriptor = hid_configuration_descriptor_gamepad;
static size_t current_hid_config_len = CONFIG_TOTAL_LEN_GAMEPAD;

static bool is_keyboard_profile = false;
static bool is_mouse_profile = false;

/* Selected profile info (for per-profile thresholds / layout) */
static const profile_info_t *s_current_profile = NULL;

void hid_reports_setup(const profile_info_t *pinfo)
{
    s_current_profile = pinfo;

    profile_type_t type = (pinfo) ? pinfo->type : PROFILE_TYPE_GAMEPAD;

    switch (type) {
    case PROFILE_TYPE_KEYBOARD_MOUSE:
        current_hid_report_descriptor = hid_report_descriptor_combo;
        current_hid_config_descriptor = hid_configuration_descriptor_combo;
        current_hid_report_desc_len = sizeof(hid_report_descriptor_combo);
        current_hid_config_len = CONFIG_TOTAL_LEN_COMBO;
        is_keyboard_profile = true;
        is_mouse_profile = true;
        ESP_LOGI(TAG, "Setup: Keyboard+Mouse (composite)");
        break;

    case PROFILE_TYPE_KEYBOARD:
        current_hid_report_descriptor = hid_report_descriptor_keyboard;
        current_hid_config_descriptor = hid_configuration_descriptor_keyboard;
        current_hid_report_desc_len = sizeof(hid_report_descriptor_keyboard);
        current_hid_config_len = CONFIG_TOTAL_LEN_KEYBOARD;
        is_keyboard_profile = true;
        is_mouse_profile = false;
        ESP_LOGI(TAG, "Setup: Keyboard only");
        break;

    default:
        current_hid_report_descriptor = hid_report_descriptor_gamepad;
        current_hid_config_descriptor = hid_configuration_descriptor_gamepad;
        current_hid_report_desc_len = sizeof(hid_report_descriptor_gamepad);
        current_hid_config_len = CONFIG_TOTAL_LEN_GAMEPAD;
        is_keyboard_profile = false;
        is_mouse_profile = false;
        ESP_LOGI(TAG, "Setup: Gamepad");
        break;
    }
}

const uint8_t *hid_reports_get_report_descriptor(size_t *len)
{
    if (len) *len = current_hid_report_desc_len;
    return current_hid_report_descriptor;
}

const uint8_t *hid_reports_get_config_descriptor(size_t *len)
{
    if (len) *len = current_hid_config_len;
    return current_hid_config_descriptor;
}

const uint8_t *hid_reports_get_msc_config_descriptor(size_t *len)
{
    if (len) *len = sizeof(msc_configuration_descriptor);
    return msc_configuration_descriptor;
}

const char **hid_reports_get_string_descriptor(size_t *count)
{
    if (count) *count = sizeof(hid_string_descriptor) / sizeof(hid_string_descriptor[0]);
    return hid_string_descriptor;
}

/* Forward declaration - defined below, used by install_tinyusb via the macro. */
static void hid_reports_usb_device_event(tinyusb_event_t *event, void *arg);

/*
 * Install TinyUSB with the given configuration descriptor.
 *
 * Kept private so main.c never includes tinyusb.h directly; this also avoids
 * the raylib/tinyusb "MOUSE_BUTTON_*" enumerator clash.
 */
static esp_err_t install_tinyusb(const uint8_t *config_desc)
{
    tinyusb_config_t cfg = TINYUSB_DEFAULT_CONFIG(hid_reports_usb_device_event);

    cfg.descriptor.device = NULL;
    cfg.descriptor.full_speed_config = config_desc;
    size_t string_count = 0;
    cfg.descriptor.string = hid_reports_get_string_descriptor(&string_count);
    cfg.descriptor.string_count = (int)string_count;
#if (TUD_OPT_HIGH_SPEED)
    cfg.descriptor.high_speed_config = config_desc;
#endif

    return tinyusb_driver_install(&cfg);
}

esp_err_t hid_reports_install_usb_hid(void)
{
    return install_tinyusb(hid_reports_get_config_descriptor(NULL));
}

esp_err_t hid_reports_install_usb_msc(void)
{
    return install_tinyusb(hid_reports_get_msc_config_descriptor(NULL));
}

bool hid_reports_usb_mounted(void)
{
    return tud_mounted();
}

/**
 * @brief Convert 12-bit ADC value to 8-bit signed gamepad axis.
 */
static int8_t adc_to_gamepad_axis(uint16_t adc_value)
{
    // deadzone around centre
    if (adc_value > 1898 && adc_value < 2198) {
        return 0;
    }
    int32_t scaled = ((int32_t)adc_value - 2048) * 127 / 2048;
    if (scaled > 127) scaled = 127;
    if (scaled < -127) scaled = -127;
    return (int8_t)scaled;
}

/* ------------------------------------------------------------------ */
/* USB HID reports                                                     */
/* ------------------------------------------------------------------ */

static void add_keycode(uint8_t *keycode_array, uint8_t keycode)
{
    if (keycode == 0) return;
    for (int i = 0; i < 6; i++) {
        if (keycode_array[i] == 0) { keycode_array[i] = keycode; return; }
        if (keycode_array[i] == keycode) return;
    }
}

void hid_reports_send_gamepad(const hid_input_state_t *s)
{
    if (!tud_mounted()) return;

    hid_gamepad_report_t rep = {0};
    rep.buttons = 0;
    rep.hat = GAMEPAD_HAT_CENTERED;

    if (s->btn_left) rep.buttons |= GAMEPAD_BUTTON_A;
    if (s->btn_right) rep.buttons |= GAMEPAD_BUTTON_B;
    if (s->btn_left_stick) rep.buttons |= GAMEPAD_BUTTON_X;
    if (s->btn_right_stick) rep.buttons |= GAMEPAD_BUTTON_Y;
    if (s->gpio_pressed) rep.buttons |= GAMEPAD_BUTTON_START;

    rep.x = adc_to_gamepad_axis(s->joy1_x);   // left X
    rep.y = adc_to_gamepad_axis(s->joy1_y);   // left Y
    rep.z = 0;
    rep.rz = 0;
    rep.rx = adc_to_gamepad_axis(s->joy2_x);  // right X
    rep.ry = adc_to_gamepad_axis(s->joy2_y);  // right Y

    tud_hid_report(REPORT_ID_GAMEPAD, &rep, sizeof(rep));
}

void hid_reports_send_keyboard(const hid_input_state_t *s)
{
    if (!tud_mounted() || !is_keyboard_profile) return;

    uint8_t keycode_array[6] = {0};
    uint8_t modifier = 0;

    // I2C / GPIO buttons as keys (with mouse-button skipping for composite mode)
    if (s->btn_left) {
        if (is_mouse_profile) add_keycode(keycode_array, HID_KEY_E);
        else add_keycode(keycode_array, HID_KEY_ESCAPE);
    }
    if (s->btn_right) {
        if (!is_mouse_profile) add_keycode(keycode_array, HID_KEY_ENTER);
    }
    if (s->btn_left_stick) {
        if (is_mouse_profile) add_keycode(keycode_array, HID_KEY_ESCAPE);
        else add_keycode(keycode_array, HID_KEY_E);
    }
    if (s->btn_right_stick) {
        if (!is_mouse_profile) add_keycode(keycode_array, HID_KEY_SPACE);
    }
    if (s->gpio_pressed) add_keycode(keycode_array, HID_KEY_SPACE);

    // Joystick-driven keys, using the selected profile's layout + threshold
    const profile_info_t *pi = s_current_profile;
    float threshold = (pi) ? pi->joystick_threshold : 0.5f;
    int8_t th = (int8_t)(threshold * 127);

    profile_type_t type = (pi) ? pi->type : PROFILE_TYPE_GAMEPAD;
    bool is_arrows = (type == PROFILE_TYPE_KEYBOARD);

    int8_t jx = adc_to_gamepad_axis(s->joy1_x);
    int8_t jy = adc_to_gamepad_axis(s->joy1_y);
    int8_t j2x = adc_to_gamepad_axis(s->joy2_x);
    int8_t j2y = adc_to_gamepad_axis(s->joy2_y);

    if (is_arrows) {
        if (jx < -th) add_keycode(keycode_array, HID_KEY_ARROW_LEFT);
        if (jx >  th) add_keycode(keycode_array, HID_KEY_ARROW_RIGHT);
        if (jy < -th) add_keycode(keycode_array, HID_KEY_ARROW_UP);
        if (jy >  th) add_keycode(keycode_array, HID_KEY_ARROW_DOWN);

        if (j2x < -th) add_keycode(keycode_array, HID_KEY_ESCAPE);
        if (j2x >  th) add_keycode(keycode_array, HID_KEY_ENTER);
        if (j2y < -th) add_keycode(keycode_array, HID_KEY_E);
        if (j2y >  th) add_keycode(keycode_array, HID_KEY_SPACE);
    } else {
        // WASD
        if (jx < -th) add_keycode(keycode_array, HID_KEY_A);
        if (jx >  th) add_keycode(keycode_array, HID_KEY_D);
        if (jy < -th) add_keycode(keycode_array, HID_KEY_W);
        if (jy >  th) add_keycode(keycode_array, HID_KEY_S);
    }

    uint8_t report_id = is_mouse_profile ? REPORT_ID_KEYBOARD : HID_ITF_PROTOCOL_KEYBOARD;
    tud_hid_keyboard_report(report_id, modifier, keycode_array);
}

void hid_reports_send_mouse(const hid_input_state_t *s)
{
    if (!tud_mounted() || !is_mouse_profile) return;

    hid_mouse_report_t rep = {0};

    const profile_info_t *pi = s_current_profile;
    float threshold = (pi) ? pi->joystick_threshold : 0.5f;
    int8_t th = (int8_t)(threshold * 127);

    int8_t j2x = adc_to_gamepad_axis(s->joy2_x);
    int8_t j2y = adc_to_gamepad_axis(s->joy2_y);

    if (abs(j2x) < th) j2x = 0;
    if (abs(j2y) < th) j2y = 0;

    if (j2x != 0) {
        rep.x = j2x / 8;
        if (rep.x == 0) rep.x = (j2x > 0) ? 1 : -1;
    }
    if (j2y != 0) {
        rep.y = j2y / 8;
        if (rep.y == 0) rep.y = (j2y > 0) ? 1 : -1;
    }

    if (s->btn_right_stick) rep.buttons |= 0x01; // left click
    if (s->btn_right)       rep.buttons |= 0x02; // right click
    if (s->btn_left_stick)  rep.buttons |= 0x04; // middle click

    static uint8_t last_buttons = 0;
    if (rep.x != 0 || rep.y != 0 || rep.buttons != last_buttons) {
        tud_hid_mouse_report(REPORT_ID_MOUSE, rep.buttons, rep.x, rep.y, rep.wheel, 0);
        last_buttons = rep.buttons;
    }
}

/* ------------------------------------------------------------------ */
/* BLE HID reports                                                     */
/* ------------------------------------------------------------------ */

void hid_reports_send_gamepad_ble(const hid_input_state_t *s)
{
    if (!ble_hid_is_connected()) return;

    uint16_t buttons = 0;
    if (s->btn_left) buttons |= GAMEPAD_BUTTON_A;
    if (s->btn_right) buttons |= GAMEPAD_BUTTON_B;
    if (s->btn_left_stick) buttons |= GAMEPAD_BUTTON_X;
    if (s->btn_right_stick) buttons |= GAMEPAD_BUTTON_Y;
    if (s->gpio_pressed) buttons |= GAMEPAD_BUTTON_START;

    ble_hid_send_gamepad(buttons,
                         adc_to_gamepad_axis(s->joy1_x),
                         adc_to_gamepad_axis(s->joy1_y),
                         adc_to_gamepad_axis(s->joy2_x),
                         adc_to_gamepad_axis(s->joy2_y));
}

void hid_reports_send_keyboard_ble(const hid_input_state_t *s)
{
    if (!ble_hid_is_connected()) return;

    uint8_t keycodes[6] = {0};
    uint8_t num = 0;
    uint8_t modifier = 0;

    int8_t jx = adc_to_gamepad_axis(s->joy1_x);
    int8_t jy = adc_to_gamepad_axis(s->joy1_y);

    if (jx < -50) keycodes[num++] = HID_KEY_ARROW_LEFT;
    else if (jx > 50) keycodes[num++] = HID_KEY_ARROW_RIGHT;
    if (jy < -50) keycodes[num++] = HID_KEY_ARROW_UP;
    else if (jy > 50) keycodes[num++] = HID_KEY_ARROW_DOWN;

    if (s->btn_left) keycodes[num++] = HID_KEY_SPACE;
    if (s->btn_right) modifier |= KEYBOARD_MODIFIER_LEFTCTRL;

    ble_hid_send_keyboard(modifier, keycodes, num);
}

void hid_reports_send_mouse_ble(const hid_input_state_t *s)
{
    if (!ble_hid_is_connected()) return;

    int8_t mx = 0, my = 0;
    uint8_t buttons = 0;

    int8_t j2x = adc_to_gamepad_axis(s->joy2_x);
    int8_t j2y = adc_to_gamepad_axis(s->joy2_y);

    if (abs(j2x) > 10) { mx = j2x / 4; if (mx == 0) mx = (j2x > 0) ? 1 : -1; }
    if (abs(j2y) > 10) { my = j2y / 4; if (my == 0) my = (j2y > 0) ? 1 : -1; }

    if (s->btn_right_stick) buttons |= 0x01;
    if (s->btn_right)       buttons |= 0x02;
    if (s->btn_left_stick)  buttons |= 0x04;

    static uint8_t last_buttons = 0;
    if (mx != 0 || my != 0 || buttons != last_buttons) {
        ble_hid_send_mouse(buttons, mx, my);
        last_buttons = buttons;
    }
}

/* ------------------------------------------------------------------ */
/* TinyUSB event + callbacks                                           */
/* ------------------------------------------------------------------ */

void hid_reports_usb_device_event(tinyusb_event_t *event, void *arg)
{
    (void)arg;
    if (event && event->id == TINYUSB_EVENT_DETACHED) {
        ESP_LOGI(TAG, "USB device detached by host - restarting system");
        esp_restart();
    }
}



uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance)
{
    (void)instance;
    return current_hid_report_descriptor;
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
                               hid_report_type_t report_type,
                               uint8_t *buffer, uint16_t reqlen)
{
    (void)instance; (void)report_id; (void)report_type;
    (void)buffer; (void)reqlen;
    return 0;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
                           hid_report_type_t report_type,
                           uint8_t const *buffer, uint16_t bufsize)
{
    (void)instance; (void)report_id; (void)report_type;
    (void)buffer; (void)bufsize;
}
