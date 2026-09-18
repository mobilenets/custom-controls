#include "fcn_status.h"

#include "esp_err.h"
#include "esp_log.h"

static const char *TAG = "status_driver";

esp_err_t fcn_board_status_init(void)
{
    ESP_LOGI(
        TAG,
        "No physical status indicator configured"
    );

    return ESP_OK;
}

esp_err_t fcn_board_status_set(fcn_status_t status)
{
    ESP_LOGD(
        TAG,
        "FCN status changed to %d",
        (int)status
    );

    return ESP_OK;
}