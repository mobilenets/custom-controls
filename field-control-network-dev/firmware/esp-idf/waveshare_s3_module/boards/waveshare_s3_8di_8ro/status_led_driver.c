#include "fcn_status.h"
#include "board_profile.h"

#include "esp_err.h"
#include "esp_log.h"

#include "led_strip.h"


static const char *TAG = "fcn_status_led";

static led_strip_handle_t status_led = NULL;


esp_err_t fcn_board_status_init(void)
{
    led_strip_config_t strip_config = {
        .strip_gpio_num = FCN_STATUS_LED_GPIO,
        .max_leds = 1,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_RGB,
        .flags = {
            .invert_out = false,
        },
    };

    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
        .mem_block_symbols = 0,
        .flags = {
            .with_dma = false,
        },
    };

    esp_err_t err =
        led_strip_new_rmt_device(
            &strip_config,
            &rmt_config,
            &status_led
        );

    if (err != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "Failed to initialize status LED: %s",
            esp_err_to_name(err)
        );

        return err;
    }

    /*
     * Always begin dark. A lit LED therefore means
     * the firmware deliberately entered a known state.
     */
    err = led_strip_clear(status_led);

    if (err == ESP_OK)
    {
        ESP_LOGI(
            TAG,
            "Status LED ready on GPIO %d",
            FCN_STATUS_LED_GPIO
        );
    }

    return err;
}


esp_err_t fcn_board_status_set(fcn_status_t status)
{
    if (status_led == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err;

    switch (status)
    {
        case FCN_STATUS_AGENT_CONNECTED:

            /*
             * Solid blue:
             * network available + agent reachable +
             * ROS entities successfully created.
             */
            err = led_strip_set_pixel(
                status_led,
                0,
                0,
                0,
                32
            );

            if (err != ESP_OK)
            {
                return err;
            }

            err = led_strip_refresh(status_led);

            if (err == ESP_OK)
            {
                ESP_LOGI(TAG, "Status: AGENT CONNECTED");
            }

            return err;


        case FCN_STATUS_OFF:
        default:

            err = led_strip_clear(status_led);

            if (err == ESP_OK)
            {
                ESP_LOGI(TAG, "Status: OFF");
            }

            return err;
    }
}
