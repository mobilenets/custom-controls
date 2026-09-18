#include <stdio.h>

#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_log.h"
#include "esp_system.h"


#include "fcn_relay.h"

static const char *TAG = "FCN";


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

    ESP_ERROR_CHECK(fcn_relay_init());

    ESP_LOGI(
        TAG,
        "FCN output mask: 0x%02X",
        fcn_relay_get_mask()
    );

   


}