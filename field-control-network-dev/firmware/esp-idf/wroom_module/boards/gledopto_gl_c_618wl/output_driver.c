#include "fcn_relay.h"
#include "board_profile.h"

#include "driver/gpio.h"

#include "esp_err.h"
#include "esp_log.h"

#include <stdbool.h>
#include <stdint.h>


static const char *TAG = "output_driver";

static uint8_t output_mask = 0x00;


/*
 * Translate FCN output number to physical GPIO.
 *
 * FCN output numbering is 1-based.
 */
static int output_gpio(uint8_t output_number)
{
    switch (output_number)
    {
        case 1:
            return FCN_OUTPUT1_GPIO;

        case 2:
            return FCN_OUTPUT2_GPIO;

        case 3:
            return FCN_OUTPUT3_GPIO;

        case 4:
            return FCN_OUTPUT4_GPIO;

        default:
            return -1;
    }
}


esp_err_t fcn_relay_init(void)
{
    ESP_LOGI(
        TAG,
        "Initializing %d amplified outputs",
        FCN_OUTPUT_COUNT
    );

    /*
     * SAFE STARTUP
     *
     * Preload all four GPIO output latches LOW before
     * configuring the pins as outputs.
     *
     * These outputs are physically active HIGH, so LOW
     * represents the fail-safe OFF state.
     */
    gpio_set_level(FCN_OUTPUT1_GPIO, FCN_OUTPUT_INACTIVE_LEVEL);
    gpio_set_level(FCN_OUTPUT2_GPIO, FCN_OUTPUT_INACTIVE_LEVEL);
    gpio_set_level(FCN_OUTPUT3_GPIO, FCN_OUTPUT_INACTIVE_LEVEL);
    gpio_set_level(FCN_OUTPUT4_GPIO, FCN_OUTPUT_INACTIVE_LEVEL);


    uint64_t output_pin_mask =
        (1ULL << FCN_OUTPUT1_GPIO) |
        (1ULL << FCN_OUTPUT2_GPIO) |
        (1ULL << FCN_OUTPUT3_GPIO) |
        (1ULL << FCN_OUTPUT4_GPIO);


    gpio_config_t config = {
        .pin_bit_mask = output_pin_mask,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };


    esp_err_t err = gpio_config(&config);

    if (err != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to configure outputs: %s",
            esp_err_to_name(err)
        );

        return err;
    }


    /*
     * Explicitly enforce OFF once more after the GPIO
     * configuration has completed.
     */
    gpio_set_level(FCN_OUTPUT1_GPIO, FCN_OUTPUT_INACTIVE_LEVEL);
    gpio_set_level(FCN_OUTPUT2_GPIO, FCN_OUTPUT_INACTIVE_LEVEL);
    gpio_set_level(FCN_OUTPUT3_GPIO, FCN_OUTPUT_INACTIVE_LEVEL);
    gpio_set_level(FCN_OUTPUT4_GPIO, FCN_OUTPUT_INACTIVE_LEVEL);

    output_mask = 0x00;

    ESP_LOGI(
        TAG,
        "Amplified outputs initialized SAFE OFF"
    );

    return ESP_OK;
}


esp_err_t fcn_relay_set(uint8_t relay_number, bool on)
{
    if (
        relay_number == 0 ||
        relay_number > FCN_OUTPUT_COUNT
    ) {
        return ESP_ERR_INVALID_ARG;
    }


    int gpio = output_gpio(relay_number);

    if (gpio < 0) {
        return ESP_ERR_INVALID_ARG;
    }


    esp_err_t err = gpio_set_level(
        gpio,
        on
            ? FCN_OUTPUT_ACTIVE_LEVEL
            : FCN_OUTPUT_INACTIVE_LEVEL
    );

    if (err != ESP_OK) {
        return err;
    }


    uint8_t bit =
        1U << (relay_number - 1);

    if (on) {
        output_mask |= bit;
    } else {
        output_mask &= ~bit;
    }


    return ESP_OK;
}


int fcn_relay_get(uint8_t relay_number)
{
    if (
        relay_number == 0 ||
        relay_number > FCN_OUTPUT_COUNT
    ) {
        return -1;
    }


    uint8_t bit =
        1U << (relay_number - 1);

    return (output_mask & bit) ? 1 : 0;
}


esp_err_t fcn_relay_refresh(void)
{
    /*
     * Direct GPIO outputs require no external refresh.
     *
     * output_mask represents the state commanded by FCN.
     */
    return ESP_OK;
}


esp_err_t fcn_relay_all_off(void)
{
    gpio_set_level(
        FCN_OUTPUT1_GPIO,
        FCN_OUTPUT_INACTIVE_LEVEL
    );

    gpio_set_level(
        FCN_OUTPUT2_GPIO,
        FCN_OUTPUT_INACTIVE_LEVEL
    );

    gpio_set_level(
        FCN_OUTPUT3_GPIO,
        FCN_OUTPUT_INACTIVE_LEVEL
    );

    gpio_set_level(
        FCN_OUTPUT4_GPIO,
        FCN_OUTPUT_INACTIVE_LEVEL
    );

    output_mask = 0x00;

    return ESP_OK;
}


uint8_t fcn_relay_get_mask(void)
{
    return output_mask;
}
