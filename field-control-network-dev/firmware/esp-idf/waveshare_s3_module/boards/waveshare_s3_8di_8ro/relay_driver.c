#include "fcn_relay.h"
#include "board_profile.h"

#include "driver/i2c_master.h"

#include "esp_err.h"
#include "esp_log.h"

#include <stdint.h>


static const char *TAG = "relay_driver";


static i2c_master_bus_handle_t i2c_bus = NULL;
static i2c_master_dev_handle_t relay_device = NULL;

static uint8_t relay_mask = 0x00;


/*
 * Write one register in the onboard relay controller.
 */
static esp_err_t relay_write_register(uint8_t reg, uint8_t value)
{
    uint8_t data[2] = {
        reg,
        value
    };

    return i2c_master_transmit(
        relay_device,
        data,
        sizeof(data),
        100
    );
}


/*
 * Read one register.
 */
static esp_err_t relay_read_register(uint8_t reg, uint8_t *value)
{
    if (value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    return i2c_master_transmit_receive(
        relay_device,
        &reg,
        1,
        value,
        1,
        100
    );
}


esp_err_t fcn_relay_init(void)
{
    ESP_LOGI(TAG, "Initializing onboard relay controller");

    ESP_LOGI(
        TAG,
        "I2C SDA=%d SCL=%d address=0x%02X",
        FCN_I2C_SDA_GPIO,
        FCN_I2C_SCL_GPIO,
        FCN_RELAY_EXPANDER_ADDR
    );


    /*
     * Initialize the ESP-IDF I2C master bus.
     */
    i2c_master_bus_config_t bus_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_NUM_0,
        .scl_io_num = FCN_I2C_SCL_GPIO,
        .sda_io_num = FCN_I2C_SDA_GPIO,
        .glitch_ignore_cnt = 7,

        /*
         * The Waveshare board should already contain its own
         * I2C pull-ups, but enabling the ESP32 internal pull-ups
         * gives us additional protection during bring-up.
         */
        .flags.enable_internal_pullup = true,
    };


    esp_err_t err = i2c_new_master_bus(
        &bus_config,
        &i2c_bus
    );

    if (err != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to initialize I2C bus: %s",
            esp_err_to_name(err)
        );

        return err;
    }


    /*
     * Probe the known board address before attaching the device.
     */
    ESP_LOGI(
        TAG,
        "Probing onboard relay controller at 0x%02X",
        FCN_RELAY_EXPANDER_ADDR
    );

    err = i2c_master_probe(
        i2c_bus,
        FCN_RELAY_EXPANDER_ADDR,
        100
    );

    if (err != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Relay controller not detected at 0x%02X: %s",
            FCN_RELAY_EXPANDER_ADDR,
            esp_err_to_name(err)
        );

        return err;
    }

    ESP_LOGI(TAG, "Onboard relay controller detected");


    /*
     * Register the device with the bus.
     */
    i2c_device_config_t device_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = FCN_RELAY_EXPANDER_ADDR,
        .scl_speed_hz = 100000,
    };


    err = i2c_master_bus_add_device(
        i2c_bus,
        &device_config,
        &relay_device
    );

    if (err != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to attach relay controller: %s",
            esp_err_to_name(err)
        );

        return err;
    }


    /*
     * SAFE STARTUP ORDER
     *
     * Set output latch to OFF before configuring pins as outputs.
     *
     * This preserves the safety behavior from the Arduino firmware.
     */
    relay_mask = 0x00;

    err = relay_write_register(
        FCN_RELAY_REG_OUTPUT,
        relay_mask
    );

    if (err != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to preload relay OFF state: %s",
            esp_err_to_name(err)
        );

        return err;
    }


    /*
     * No polarity inversion.
     */
    err = relay_write_register(
        FCN_RELAY_REG_POLARITY,
        0x00
    );

    if (err != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to configure polarity: %s",
            esp_err_to_name(err)
        );

        return err;
    }


    /*
     * Configuration register:
     *
     * 0 = output
     * 1 = input
     *
     * Set all 8 controller pins to outputs.
     */
    err = relay_write_register(
        FCN_RELAY_REG_CONFIG,
        0x00
    );

    if (err != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to configure relay outputs: %s",
            esp_err_to_name(err)
        );

        return err;
    }


    /*
     * Read the output register back for confirmation.
     */
    uint8_t readback = 0xFF;

    err = relay_read_register(
        FCN_RELAY_REG_OUTPUT,
        &readback
    );

    if (err != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to read relay state: %s",
            esp_err_to_name(err)
        );

        return err;
    }


    relay_mask = readback;

    ESP_LOGI(
        TAG,
        "Relay output mask: 0x%02X",
        relay_mask
    );

    ESP_LOGI(
        TAG,
        "Onboard relays initialized SAFE OFF"
    );

    return ESP_OK;
}


esp_err_t fcn_relay_set(uint8_t relay_number, bool on)
{
    if (relay_device == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (
        relay_number == 0 ||
        relay_number > FCN_ONBOARD_RELAY_COUNT
    ) {
        return ESP_ERR_INVALID_ARG;
    }


    uint8_t bit = 1U << (relay_number - 1);


    uint8_t new_mask = relay_mask;

    if (on) {
        new_mask |= bit;
    } else {
        new_mask &= ~bit;
    }


    esp_err_t err = relay_write_register(
        FCN_RELAY_REG_OUTPUT,
        new_mask
    );

    if (err != ESP_OK) {
        return err;
    }


    relay_mask = new_mask;

    return ESP_OK;
}


esp_err_t fcn_relay_all_off(void)
{
    if (relay_device == NULL) {
        return ESP_ERR_INVALID_STATE;
    }


    esp_err_t err = relay_write_register(
        FCN_RELAY_REG_OUTPUT,
        0x00
    );

    if (err == ESP_OK) {
        relay_mask = 0x00;
    }


    return err;
}


uint8_t fcn_relay_get_mask(void)
{
    return relay_mask;
}
