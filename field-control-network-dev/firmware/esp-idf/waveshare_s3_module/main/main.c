#include <stdio.h>

#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_log.h"

#include "board_profile.h"
#include "fcn_config.h"
#include "fcn_relay.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "fcn_input.h"
static const char *TAG = "FCN";


void app_main(void)
{
    esp_chip_info_t chip_info;
    uint32_t flash_size = 0;

    esp_chip_info(&chip_info);

    ESP_ERROR_CHECK(
        esp_flash_get_size(NULL, &flash_size)
    );

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Field Control Network");
    ESP_LOGI(TAG, "Native ESP-IDF node");
    ESP_LOGI(TAG, "========================================");

    ESP_LOGI(TAG, "Board: %s", FCN_BOARD_NAME);
    ESP_LOGI(TAG, "Board ID: %s", FCN_BOARD_ID);

    ESP_LOGI(TAG,
             "Chip cores: %d, revision: %d",
             chip_info.cores,
             chip_info.revision);

    ESP_LOGI(TAG,
             "Flash size: %lu bytes",
             (unsigned long)flash_size);

    ESP_LOGI(TAG, "--- Board capabilities ---");

    ESP_LOGI(TAG, "Wi-Fi:          %s",
             FCN_HAS_WIFI ? "YES" : "NO");

    ESP_LOGI(TAG, "Ethernet:       %s",
             FCN_HAS_ETHERNET ? "YES" : "NO");

    ESP_LOGI(TAG, "Relays:         %s",
             FCN_HAS_RELAYS ? "YES" : "NO");

    ESP_LOGI(TAG, "Digital inputs: %s",
             FCN_HAS_DIGITAL_INPUTS ? "YES" : "NO");

    ESP_LOGI(TAG, "RS485:          %s",
             FCN_HAS_RS485 ? "YES" : "NO");

    ESP_LOGI(TAG,
             "Onboard relays: %d",
             FCN_ONBOARD_RELAY_COUNT);

    ESP_LOGI(TAG,
             "Digital inputs: %d",
             FCN_DIGITAL_INPUT_COUNT);

    fcn_config_t config;
    fcn_config_init(&config);

    ESP_LOGI(TAG,
             "Default micro-ROS agent port: %u",
             config.agent_port);

    ESP_LOGI(TAG, "--- Onboard relay subsystem ---");

    esp_err_t relay_err = fcn_relay_init();

    if (relay_err == ESP_OK) {

        ESP_LOGI(
            TAG,
            "Relay subsystem ready, mask=0x%02X",
            fcn_relay_get_mask()
        );

    } else {

        ESP_LOGE(
            TAG,
            "Relay subsystem initialization failed: %s",
            esp_err_to_name(relay_err)
        );
    }

    /* if (relay_err == ESP_OK)
    {
        ESP_LOGI(TAG, "--- Controlled relay test ---");

        ESP_LOGI(TAG, "Relay 1 ON");

        esp_err_t test_err = fcn_relay_set(1, true);

        if (test_err == ESP_OK)
        {
            ESP_LOGI(
                TAG,
                "Relay mask: 0x%02X",
                fcn_relay_get_mask()
            );
        }
        else
        {
            ESP_LOGE(
                TAG,
                "Failed to turn relay 1 ON: %s",
                esp_err_to_name(test_err)
            );
        }


        vTaskDelay(pdMS_TO_TICKS(2000));


        ESP_LOGI(TAG, "Relay 1 OFF");

        test_err = fcn_relay_set(1, false);

        if (test_err == ESP_OK)
        {
            ESP_LOGI(
                TAG,
                "Relay mask: 0x%02X",
                fcn_relay_get_mask()
            );
        }
        else
        {
            ESP_LOGE(
                TAG,
                "Failed to turn relay 1 OFF: %s",
                esp_err_to_name(test_err)
            );
        }
    } */

    ESP_LOGI(TAG, "--- Digital input subsystem ---");

    esp_err_t input_err = fcn_input_init();

   if (input_err == ESP_OK)
    {
        uint8_t input_mask = fcn_input_get_mask();

        ESP_LOGI(
            TAG,
            "Initial digital input mask: 0x%02X",
            input_mask
        );

        esp_err_t monitor_err = fcn_input_start_monitor();

        if (monitor_err != ESP_OK)
        {
            ESP_LOGE(
                TAG,
                "Failed to start input monitor: %s",
                esp_err_to_name(monitor_err)
            );
        }
    }
    else
    {
        ESP_LOGE(
            TAG,
            "Digital input subsystem failed: %s",
            esp_err_to_name(input_err)
        );
    }

    ESP_LOGI(TAG, "FCN initialization complete.");
}
