# AtomS3R USB Joystick (ESP32-S3)

A HID game controller and multi-mode peripheral firmware for the
[M5Stack AtomS3R](https://m5stack.com/product/t-atom-atom-s3r) (ESP32-S3) board.
The firmware is built on top of [raylib](https://www.raylib.com/) with its
software renderer driving the on-panel LCD, and exposes the device as a USB HID
controller, a BLE HID controller, an MSC storage device, and a small Snake game.

Device state is chosen through a profile-selection menu rendered on the built-in
display. Each profile is a named configuration that selects which mode the board
enters when it enumerates.

## Features

- Profile-selection menu rendered on the 128x128 ST7789 display.
- USB HID: gamepad, keyboard, and mouse reports (via libtinyusb).
- BLE HID: gamepad, keyboard, and mouse over Bluetooth.
- USB Mass Storage device.
- Snake mini-game selectable from the menu.
- Profiles loaded from on-board storage or a built-in default set.
- Backlight driven through the on-board LP5562 LED controller.

## Hardware

This project targets the AtomS3R module and its peripherals. The relevant wiring
is fixed by the board and is documented below.

### Display

The module ships with a GC9107 panel, but the driver is ported against the ST7789
SPI driver (a compatible Sitronix controller) available in ESP-IDF.

| Signal | GPIO |
| --------- | --------- |
| SPI bus | SPI3_HOST |
| MOSI | GPIO21 |
| SCLK | GPIO15 |
| CS | GPIO14 |
| DC | GPIO42 |
| RST | GPIO48 |
| Resolution | 128x128, RGB ordered as BGR |

The panel is reset, initialized, and flipped (both axes) to match the physical
panel orientation, and the display backplane is enabled after initialization.

### Backlight

The backlight is not a plain GPIO: the white channel is driven by an LP5562
constant-current LED controller on I2C. The driver enables the chip and sets the
white-channel PWM value so the panel lights.

| Signal | GPIO / address |
| --------- | --------- |
| I2C bus | GPIO0 (SCL), GPIO45 (SDA) |
| Device address | 0x30 (7-bit) |

### Input

| Signal | GPIO / address |
| --------- | --------- |
| I2C joystick | 0x59 (I2C, 400 kHz) |
| Joystick SCL | GPIO39 |
| Joystick SDA | GPIO38 |
| Select button | GPIO41 (active low) |

## Profiles

Profiles are read from storage at startup. If storage is empty or unreadable the
firmware falls back to the built-in default profiles. Each default profile maps to
a mode through its type and connection mode:

| Profile | Type | Connection | Resulting mode |
| --------- | --------- | --------- | --------- |
| gamepad | gamepad | USB | USB HID gamepad |
| wasd | keyboard/mouse | USB | USB HID keyboard + mouse |
| arrows | keyboard | USB | USB HID keyboard |
| Mass Storage | (unknown) | USB | USB Mass Storage |
| Snake Game | (unknown) | auto | Snake game |

The menu scrolls automatically so the selected entry is always visible when the
list is longer than the display. Move the cursor up and down to browse, the
selection is highlighted, and a bright bar follows the list as it scrolls.

## Architecture

- `main/main.c`: entry point, display and peripheral initialization, the
  application state machine (`APP_STATE_MENU`, `APP_STATE_USB_HID`,
  `APP_STATE_BLE_HID`, `APP_STATE_MSC`, `APP_STATE_SNAKE`), and the raylib render
  loop.
- `main/main.c` display section: ST7789 SPI initialization and the raylib flush
  callback that chunks the framebuffer for the LCD transfer.
- `main/backlight.c`: LP5562 I2C backlight driver.
- `main/i2c_joystick.c`: I2C joystick input driver.
- `main/gpio_input.c`: GPIO button input sampling.
- `main/profile_menu.c`: Raylib profile-selection menu (rendering, navigation,
  scrolling).
- `main/profile_parser.c`: profile loading and parsing, with the built-in default
  set.
- `main/hid_reports.c`: USB HID report generation.
- `main/ble_hid.c`: BLE HID reporting.
- `main/snake_game.c`: Snake game logic.
- `main/ui.c`: status and USB-active screen rendering helpers.

The display initialization and framebuffer flush are preserved verbatim from the
reference M5Stack AtomS3R raylib example, so the rendering path matches the
validated hardware configuration.

## Build

The project uses the ESP-IDF build system. Set `IDF_PATH` to your ESP-IDF
checkout (v6.0 or newer) and run from the project directory:

```bash
idf.py set-target esp32s3
idf.py build
```

## Flash

Flash to the device with your serial port:

```bash
idf.py -p /dev/ttyACM0 flash monitor
```

The device enumerates and the profile menu appears on the display once the
firmware starts.

