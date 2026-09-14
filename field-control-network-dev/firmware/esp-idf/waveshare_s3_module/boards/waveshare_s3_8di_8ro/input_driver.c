#include "fcn_input.h"
#include "board_profile.h"

#include "driver/gpio.h"
#include "esp_log.h"

#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

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

#define FCN_INPUT_SCAN_INTERVAL_MS 50

static void input_monitor_task(void *arg)
{
    uint8_t last_mask = fcn_input_get_mask();

    ESP_LOGI(
        TAG,
        "Input monitor started, initial mask=0x%02X",
        last_mask
    );

    while (1)
    {
        uint8_t current_mask = fcn_input_get_mask();

        if (current_mask != last_mask)
        {
            uint8_t changed = current_mask ^ last_mask;

            for (uint8_t i = 0; i < FCN_DIGITAL_INPUT_COUNT; i++)
            {
                uint8_t bit = (1U << i);

                if (changed & bit)
                {
                    int level = (current_mask & bit) ? 1 : 0;

                    ESP_LOGI(
                        TAG,
                        "Input %u changed -> %d",
                        i + 1,
                        level
                    );
                }
            }

            ESP_LOGI(
                TAG,
                "Input mask: 0x%02X",
                current_mask
            );

            last_mask = current_mask;
        }

        vTaskDelay(pdMS_TO_TICKS(FCN_INPUT_SCAN_INTERVAL_MS));
    }
}

esp_err_t fcn_input_start_monitor(void)
{
    BaseType_t result = xTaskCreate(
        input_monitor_task,
        "fcn_input_monitor",
        3072,
        NULL,
        5,
        NULL
    );

    if (result != pdPASS)
    {
        ESP_LOGE(TAG, "Failed to start input monitor task");
        return ESP_FAIL;
    }

    return ESP_OK;
}
