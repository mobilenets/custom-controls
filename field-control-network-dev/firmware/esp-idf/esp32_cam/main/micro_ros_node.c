#define __STDC_WANT_LIB_EXT1__ 0
#include "micro_ros_node.h"
#include <rcl/rcl.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"

#include <rmw_microros/rmw_microros.h>

#define AGENT_IP   "10.0.0.222"
#define AGENT_PORT "8888"

static const char *TAG = "micro_ros";

static void micro_ros_task(void *arg)
{
    ESP_LOGI(TAG, "micro-ROS task started");

    rcl_allocator_t allocator = rcl_get_default_allocator();

    rcl_init_options_t init_options =
        rcl_get_zero_initialized_init_options();

    rcl_ret_t rc =
        rcl_init_options_init(
            &init_options,
            allocator
        );

    if (rc != RCL_RET_OK) {
        ESP_LOGE(TAG, "rcl_init_options_init failed");
        vTaskDelete(NULL);
        return;
    }

    rmw_init_options_t *rmw_options =
        rcl_init_options_get_rmw_init_options(
            &init_options
        );

    rmw_ret_t rmw_rc =
        rmw_uros_options_set_udp_address(
            AGENT_IP,
            AGENT_PORT,
            rmw_options
        );

    if (rmw_rc != RMW_RET_OK) {
        ESP_LOGE(TAG, "Failed to set agent address");

        rcl_ret_t fini_rc = rcl_init_options_fini(&init_options);
        if (fini_rc != RCL_RET_OK) {
            ESP_LOGW(
                TAG,
                "rcl_init_options_fini failed: %d",
                (int)fini_rc
            );
        }

        vTaskDelete(NULL);
        return;
}

    ESP_LOGI(
        TAG,
        "Agent configured: %s:%s",
        AGENT_IP,
        AGENT_PORT
    );

    while (1) {

        rmw_rc =
            rmw_uros_ping_agent_options(
                250,
                3,
                rmw_options
            );

        if (rmw_rc == RMW_RET_OK) {
            ESP_LOGI(TAG, "Agent detected");
        }
        else {
            ESP_LOGI(TAG, "Waiting for agent...");
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void micro_ros_start(void)
{
    xTaskCreate(
        micro_ros_task,
        "micro_ros",
        12000,
        NULL,
        5,
        NULL
    );
}
