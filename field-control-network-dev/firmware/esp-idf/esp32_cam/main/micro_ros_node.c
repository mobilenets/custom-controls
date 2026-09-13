#define __STDC_WANT_LIB_EXT1__ 0

#include "micro_ros_node.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"

#include <rcl/rcl.h>
#include <rclc/rclc.h>
#include <rclc/executor.h>

#include <rmw_microros/rmw_microros.h>

#include <std_msgs/msg/int32.h>

#define AGENT_IP   "10.0.0.222"
#define AGENT_PORT "8888"

#define HEARTBEAT_TOPIC "/fcn/camera/heartbeat"
#define COMMAND_TOPIC   "/fcn/camera/command"

static const char *TAG = "micro_ros";

static rcl_publisher_t heartbeat_pub;
static rcl_subscription_t command_sub;

static std_msgs__msg__Int32 heartbeat_msg;
static std_msgs__msg__Int32 command_msg;

static void command_callback(const void *msgin)
{
    const std_msgs__msg__Int32 *msg =
        (const std_msgs__msg__Int32 *)msgin;

    ESP_LOGI(
        TAG,
        "Command received: %ld",
        (long)msg->data
    );
}

static void micro_ros_task(void *arg)
{
    (void)arg;

    ESP_LOGI(TAG, "micro-ROS task started");

    rcl_allocator_t allocator =
        rcl_get_default_allocator();

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

        rcl_ret_t fini_rc =
            rcl_init_options_fini(&init_options);

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

    while (
        rmw_uros_ping_agent_options(
            250,
            3,
            rmw_options
        ) != RMW_RET_OK
    ) {
        ESP_LOGI(TAG, "Waiting for agent...");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    ESP_LOGI(TAG, "Agent detected");

    /*
     * Initialize micro-ROS support using the
     * already-configured init options.
     */
    rclc_support_t support;

    rc = rclc_support_init_with_options(
        &support,
        0,
        NULL,
        &init_options,
        &allocator
    );

    if (rc != RCL_RET_OK) {
        ESP_LOGE(TAG, "rclc_support_init_with_options failed");
        vTaskDelete(NULL);
        return;
    }

    /*
     * Create ROS node.
     */
    rcl_node_t node;

    rc = rclc_node_init_default(
        &node,
        "esp32_camera_node",
        "",
        &support
    );

    if (rc != RCL_RET_OK) {
        ESP_LOGE(TAG, "rclc_node_init_default failed");
        vTaskDelete(NULL);
        return;
    }

    /*
     * Publisher
     */
    heartbeat_pub =
        rcl_get_zero_initialized_publisher();

    rc = rclc_publisher_init_best_effort(
        &heartbeat_pub,
        &node,
        ROSIDL_GET_MSG_TYPE_SUPPORT(
            std_msgs,
            msg,
            Int32
        ),
        HEARTBEAT_TOPIC
    );

    if (rc != RCL_RET_OK) {
        ESP_LOGE(TAG, "heartbeat publisher init failed");
        vTaskDelete(NULL);
        return;
    }

    /*
     * Subscriber
     */
    command_sub =
        rcl_get_zero_initialized_subscription();

    rc = rclc_subscription_init_best_effort(
        &command_sub,
        &node,
        ROSIDL_GET_MSG_TYPE_SUPPORT(
            std_msgs,
            msg,
            Int32
        ),
        COMMAND_TOPIC
    );

    if (rc != RCL_RET_OK) {
        ESP_LOGE(TAG, "command subscriber init failed");
        vTaskDelete(NULL);
        return;
    }

    /*
     * Executor
     */
    rclc_executor_t executor =
        rclc_executor_get_zero_initialized_executor();

    rc = rclc_executor_init(
        &executor,
        &support.context,
        1,
        &allocator
    );

    if (rc != RCL_RET_OK) {
        ESP_LOGE(TAG, "executor init failed");
        vTaskDelete(NULL);
        return;
    }

    rc = rclc_executor_add_subscription(
        &executor,
        &command_sub,
        &command_msg,
        &command_callback,
        ON_NEW_DATA
    );

    if (rc != RCL_RET_OK) {
        ESP_LOGE(TAG, "executor add subscription failed");
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "ROS entities created");
    ESP_LOGI(TAG, "Publisher : %s", HEARTBEAT_TOPIC);
    ESP_LOGI(TAG, "Subscriber: %s", COMMAND_TOPIC);

    heartbeat_msg.data = 0;

    TickType_t last_heartbeat =
        xTaskGetTickCount();

    while (1) {

        /*
         * Service incoming subscription data.
         */
        rclc_executor_spin_some(
            &executor,
            RCL_MS_TO_NS(10)
        );

        /*
         * Publish heartbeat once per second.
         */
        TickType_t now =
            xTaskGetTickCount();

        if (
            now - last_heartbeat >=
            pdMS_TO_TICKS(1000)
        ) {
            heartbeat_msg.data++;

            rc = rcl_publish(
                &heartbeat_pub,
                &heartbeat_msg,
                NULL
            );

            if (rc == RCL_RET_OK) {
                ESP_LOGI(
                    TAG,
                    "Heartbeat published: %ld",
                    (long)heartbeat_msg.data
                );
            }
            else {
                ESP_LOGW(
                    TAG,
                    "Heartbeat publish failed: %d",
                    (int)rc
                );
            }

            last_heartbeat = now;
        }

        vTaskDelay(pdMS_TO_TICKS(10));
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