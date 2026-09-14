/*
 * I2C Joystick Driver for StampFly Controller
 * Ported from Arduino atoms3joy library to ESP-IDF
 */

#include "i2c_joystick.h"
#include "freertos/FreeRTOS.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "i2c_joystick";

esp_err_t i2c_joystick_init(i2c_joystick_handle_t *stick) {
    esp_err_t ret;

    // I2C bus configuration
    i2c_master_bus_config_t bus_config = {
        .scl_io_num = I2C_SCL_GPIO,
        .sda_io_num = I2C_SDA_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
    };

    // Create I2C bus
    ret = i2c_new_master_bus(&bus_config, &stick->bus_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create I2C bus: %s", esp_err_to_name(ret));
        return ret;
    }

    // I2C device configuration
    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = I2C_JOYSTICK_ADDR,
        .scl_speed_hz = I2C_FREQ_HZ,
    };

    // Add device to bus
    ret = i2c_master_bus_add_device(stick->bus_handle, &dev_config, &stick->dev_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add I2C device: %s", esp_err_to_name(ret));
        i2c_del_master_bus(stick->bus_handle);
        return ret;
    }

    // Probe for device
    ret = i2c_master_probe(stick->bus_handle, I2C_JOYSTICK_ADDR, I2C_TIMEOUT_MS / portTICK_PERIOD_MS);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C device not found at address 0x%02X", I2C_JOYSTICK_ADDR);
        i2c_master_bus_rm_device(stick->dev_handle);
        i2c_del_master_bus(stick->bus_handle);
        return ret;
    }

    stick->initialized = true;
    ESP_LOGI(TAG, "I2C joystick initialized at 0x%02X", I2C_JOYSTICK_ADDR);

    return ESP_OK;
}

esp_err_t i2c_joystick_read_axis(i2c_joystick_handle_t *stick, uint8_t reg, uint16_t *value) {
    if (!stick->initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret;
    uint8_t reg_addr = reg;
    uint8_t data[2];

    // Write register address
    ret = i2c_master_transmit(stick->dev_handle, &reg_addr, 1, -1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write register address: %s", esp_err_to_name(ret));
        return ret;
    }

    // Read 2 bytes (little-endian)
    ret = i2c_master_receive(stick->dev_handle, data, 2, -1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read axis data: %s", esp_err_to_name(ret));
        return ret;
    }

    // Convert little-endian to uint16_t
    *value = data[0] | (data[1] << 8);

    return ESP_OK;
}

esp_err_t i2c_joystick_read_button(i2c_joystick_handle_t *stick, uint8_t button, bool *pressed) {
    if (!stick->initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (button > 3) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret;
    uint8_t reg_addr = BUTTON_1_REG + button;
    uint8_t data;

    // Write register address
    ret = i2c_master_transmit(stick->dev_handle, &reg_addr, 1, -1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write button register: %s", esp_err_to_name(ret));
        return ret;
    }

    // Read 1 byte
    ret = i2c_master_receive(stick->dev_handle, &data, 1, -1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read button data: %s", esp_err_to_name(ret));
        return ret;
    }

    // Inverted logic: 0 = pressed, 1 = released
    *pressed = (data == 0);

    return ESP_OK;
}

esp_err_t i2c_joystick_read_all(i2c_joystick_handle_t *stick,
                                 uint16_t *joy1_x, uint16_t *joy1_y,
                                 bool *btn_left, bool *btn_right,
                                 bool *btn_left_stick, bool *btn_right_stick) {
    esp_err_t ret;

    ret = i2c_joystick_read_axis(stick, JOY1_X_REG, joy1_x);
    if (ret != ESP_OK) return ret;

    ret = i2c_joystick_read_axis(stick, JOY1_Y_REG, joy1_y);
    if (ret != ESP_OK) return ret;

    ret = i2c_joystick_read_button(stick, BUTTON_LEFT, btn_left);
    if (ret != ESP_OK) return ret;

    ret = i2c_joystick_read_button(stick, BUTTON_RIGHT, btn_right);
    if (ret != ESP_OK) return ret;

    ret = i2c_joystick_read_button(stick, BUTTON_LEFT_STICK, btn_left_stick);
    if (ret != ESP_OK) return ret;

    ret = i2c_joystick_read_button(stick, BUTTON_RIGHT_STICK, btn_right_stick);
    if (ret != ESP_OK) return ret;

    return ESP_OK;
}
