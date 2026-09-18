#include "fcn_input.h"
#include "board_profile.h"

#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"

#include <stdint.h>

static const char *TAG = "input_driver";

esp_err_t fcn_input_init(void)
{
    ESP_LOGI(
        TAG,
        "Initializing %d digital input",
        FCN_DIGITAL_INPUT_COUNT
    );

    gpio_config_t config = {
        .pin_bit_mask = (1ULL << FCN_DI1_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };

    esp_err_t err = gpio_config(&config);

    if (err != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "Failed to configure input: %s",
            esp_err_to_name(err)
        );

        return err;
    }

    ESP_LOGI(
        TAG,
        "Digital input initialized on GPIO %d",
        FCN_DI1_GPIO
    );

    return ESP_OK;
}

int fcn_input_get(uint8_t input_number)
{
    if (
        input_number == 0 ||
        input_number > FCN_DIGITAL_INPUT_COUNT
    )
    {
        return -1;
    }

    return gpio_get_level(FCN_DI1_GPIO);
}

uint8_t fcn_input_get_mask(void)
{
    uint8_t mask = 0;

    if (gpio_get_level(FCN_DI1_GPIO))
    {
        mask |= 0x01;
    }

    return mask;
}

esp_err_t fcn_input_start_monitor(void)
{
    /*
     * No board-local input monitor is required yet.
     * micro-ROS reads the current input state through
     * the common FCN input interface.
     */
    return ESP_OK;
}