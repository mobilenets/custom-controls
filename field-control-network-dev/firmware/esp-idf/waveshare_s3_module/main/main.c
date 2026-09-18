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
#include "fcn_network.h"

#include "fcn_config.h"
#include "fcn_config_console.h"
#include "fcn_config_boot.h"

#include "fcn_microros.h"
#include "fcn_status.h"

#include "rs485_relay_driver.h"

static const char *TAG = "FCN";

static fcn_config_t g_config;

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

    ESP_LOGI(TAG, "--- FCN configuration ---");

    esp_err_t config_err = fcn_config_load(&g_config);

    if (config_err != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "Failed to load FCN configuration: %s",
            esp_err_to_name(config_err)
        );

        return;
    }

    ESP_LOGI(
        TAG,
        "Loaded module %u: %s",
        g_config.module_id,
        g_config.module_name
    );

    ESP_LOGI(
        TAG,
        "Configured I/O: %u inputs, %u outputs",
        g_config.input_count,
        g_config.output_count
    );

    ESP_LOGI(
        TAG,
        "Agent: %s:%u",
        g_config.agent_ip[0]
            ? g_config.agent_ip
            : "[not configured]",
        g_config.agent_port
    );

    ESP_LOGI(
        TAG,
        "Ethernet enabled: %s",
        g_config.ethernet_enabled ? "YES" : "NO"
    );

    ESP_LOGI(
        TAG,
        "Wi-Fi fallback: %s",
        g_config.wifi_fallback_enabled ? "YES" : "NO"
    );


    if (fcn_config_boot_window(5000))
    {
        esp_err_t console_err =
            fcn_config_console_run(&g_config);

        if (console_err == ESP_OK)
        {
            ESP_LOGI(
                TAG,
                "Configuration saved and reloaded"
            );
        }
        else if (console_err == ESP_ERR_INVALID_STATE)
        {
            ESP_LOGI(
                TAG,
                "Configuration cancelled"
            );
        }
        else
        {
            ESP_LOGE(
                TAG,
                "Configuration console failed: %s",
                esp_err_to_name(console_err)
            );
        }
    }

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

    ESP_LOGI(TAG, "--- RS485 expansion subsystem ---");

    esp_err_t rs485_err = rs485_relay_init();

    if (rs485_err == ESP_OK)
    {
        uint8_t rs485_coil_mask = 0;

        if (rs485_relay_detect(&rs485_coil_mask))
        {
            ESP_LOGI(
                TAG,
                "RS485 expansion available, mask=0x%02X",
                rs485_coil_mask
            );
        }
        else
        {
            ESP_LOGI(
                TAG,
                "Continuing without RS485 expansion"
            );
        }
    }
    else
    {
        ESP_LOGE(
            TAG,
            "RS485 initialization failed: %s",
            esp_err_to_name(rs485_err)
        );
    }

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

    ESP_LOGI(TAG, "--- Status subsystem ---");

    esp_err_t status_err = fcn_status_init();

    if (status_err != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "Status subsystem initialization failed: %s",
            esp_err_to_name(status_err)
        );
    }
    else
    {
        ESP_LOGI(
            TAG,
            "Status subsystem ready"
        );
    }

    ESP_LOGI(TAG, "--- Network subsystem ---");

    fcn_network_config_t network_config = {
        .ethernet_enabled =
            g_config.ethernet_enabled,

        .wifi_fallback_enabled =
            g_config.wifi_fallback_enabled,

        .wifi_ssid =
            g_config.wifi_ssid,

        .wifi_password =
            g_config.wifi_password
    };

    esp_err_t network_err =
        fcn_network_init(&network_config);

    if (network_err == ESP_OK)
    {
        ESP_LOGI(
            TAG,
            "Network manager started"
        );
    }
    else
    {
        ESP_LOGE(
            TAG,
            "Network initialization failed: %s",
            esp_err_to_name(network_err)
        );
    }

    ESP_LOGI(TAG, "--- micro-ROS subsystem ---");

    fcn_microros_config_t microros_config = {
        .agent_ip =
            g_config.agent_ip,

        .agent_port =
            g_config.agent_port,

        .module_id =
            g_config.module_id,

        .module_name =
            g_config.module_name,

        .input_count =
                g_config.input_count,

        .output_count =
            g_config.output_count
    };

    esp_err_t microros_err =
        fcn_microros_start(&microros_config);

    if (microros_err == ESP_OK)
    {
        ESP_LOGI(
            TAG,
            "micro-ROS manager started"
        );
    }
    else
    {
        ESP_LOGE(
            TAG,
            "micro-ROS initialization failed: %s",
            esp_err_to_name(microros_err)
        );
    }
    
    ESP_LOGI(TAG, "FCN initialization complete.");
}
