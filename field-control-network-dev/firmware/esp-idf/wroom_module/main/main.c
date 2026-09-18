#include <stdio.h>

#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_log.h"
#include "esp_system.h"

#include "fcn_config.h"
#include "fcn_config_boot.h"
#include "fcn_config_console.h"
#include "fcn_network.h"

#include "fcn_relay.h"

static const char *TAG = "FCN";
static fcn_config_t g_config;


void app_main(void)
{
    printf("\n");
    printf("===============================\n");
    printf(" Field Control Network\n");
    printf(" GLEDOPTO GL-C-618WL\n");
    printf(" Native ESP-IDF\n");
    printf("===============================\n\n");

    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);

    uint32_t flash_size = 0;
    esp_flash_get_size(NULL, &flash_size);

    ESP_LOGI(
        TAG,
        "CPU cores: %d",
        chip_info.cores
    );

    ESP_LOGI(
        TAG,
        "Silicon revision: %d",
        chip_info.revision
    );

    ESP_LOGI(
        TAG,
        "Flash size: %lu MB",
        (unsigned long)(flash_size / (1024 * 1024))
    );

    ESP_LOGI(
        TAG,
        "FCN alive"
    );
 
    ESP_LOGI(TAG, "Initializing FCN outputs");

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

    if (fcn_config_boot_window(5000))
    {
        esp_err_t console_err =
            fcn_config_console_run(&g_config);

        if (console_err == ESP_OK)
        {
            ESP_LOGI(TAG, "Configuration saved and reloaded");
        }
        else if (console_err == ESP_ERR_INVALID_STATE)
        {
            ESP_LOGI(TAG, "Configuration cancelled");
        }
        else
        {
            ESP_LOGE(
                TAG,
                "Configuration console failed: %s",
                esp_err_to_name(console_err)
            );

            return;
        }
    }

    ESP_ERROR_CHECK(fcn_relay_init());

    ESP_LOGI(
        TAG,
        "FCN output mask: 0x%02X",
        fcn_relay_get_mask()
    );

    ESP_LOGI(TAG, "--- Network subsystem ---");

    fcn_network_config_t network_config = {
        .ethernet_enabled = false,
        .wifi_fallback_enabled = true,
        .wifi_ssid = g_config.wifi_ssid,
        .wifi_password = g_config.wifi_password
    };

    esp_err_t network_err =
        fcn_network_init(&network_config);

    if (network_err == ESP_OK)
    {
        ESP_LOGI(TAG, "Network manager started");
    }
    else
    {
        ESP_LOGE(
            TAG,
            "Network initialization failed: %s",
            esp_err_to_name(network_err)
        );
    }


}