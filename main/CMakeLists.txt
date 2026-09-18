idf_component_register(SRCS "main.c"
                             "profile_parser.c"
                             "i2c_joystick.c"
                             "ble_hid.c"
                             "msc_storage.c"
                             "ui.c"
                             "profile_menu.c"
                             "snake_game.c"
                             "hid_reports.c"
                             "gpio_input.c"
                             "backlight.c"
                    INCLUDE_DIRS "."
                    REQUIRES georgik__raylib esp_driver_spi esp_lcd
                             espressif__esp_lcd_gc9a01
                             esp_driver_gpio esp_driver_i2c
                             esp_tinyusb wear_levelling fatfs
                             bt esp_hid nvs_flash)
