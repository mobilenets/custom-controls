#include "fcn_input.h"
#include "board_profile.h"

#include "driver/gpio.h"
#include "esp_log.h"

#include <stdint.h>


static const char *TAG = "input_driver";


static const gpio_num_t input_pins[FCN_DIGITAL_INPUT_COUNT] = {
    FCN_DI1_GPIO,
    FCN_DI2_GPIO,
    FCN_DI3_GPIO,
    FCN_DI4_GPIO,
    FCN_DI5_GPIO,
    FCN_DI6_GPIO,
    FCN_DI7_GPIO,
    FCN_DI8_GPIO
};


esp_err_t fcn_input_init(void)
{
    ESP_LOGI(TAG, "Initializing %d digital inputs",
             FCN_DIGITAL_INPUT_COUNT);

    uint64_t pin_mask = 0;

    for (int i = 0; i < FCN_DIGITAL_INPUT_COUNT; i++) {
        pin_mask |= (1ULL << input_pins[i]);
    }


    gpio_config_t config = {
        .pin_bit_mask = pin_mask,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };


    esp_err_t err = gpio_config(&config);

    if (err != ESP_OK) {
        ESP_LOGE(
            TAG,
            "GPIO configuration failed: %s",
            esp_err_to_name(err)
        );

        return err;
    }


    for (int i = 0; i < FCN_DIGITAL_INPUT_COUNT; i++) {
        ESP_LOGI(
            TAG,
            "Input %d -> GPIO %d",
            i + 1,
            input_pins[i]
        );
    }


    ESP_LOGI(TAG, "Digital inputs initialized");

    return ESP_OK;
}


int fcn_input_get(uint8_t input_number)
{
    if (
        input_number == 0 ||
        input_number > FCN_DIGITAL_INPUT_COUNT
    ) {
        return -1;
    }


    gpio_num_t gpio =
        input_pins[input_number - 1];


    return gpio_get_level(gpio);
}


uint8_t fcn_input_get_mask(void)
{
    uint8_t mask = 0;


    for (uint8_t i = 0;
         i < FCN_DIGITAL_INPUT_COUNT;
         i++)
    {
        if (gpio_get_level(input_pins[i])) {
            mask |= (1U << i);
        }
    }


    return mask;
}
