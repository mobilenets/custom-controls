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

#define AGENT_PING_TIMEOUT_MS       250
#define AGENT_PING_ATTEMPTS         3
#define AGENT_FAILURE_LIMIT         3

#define WAITING_AGENT_DELAY_MS      1000
#define CONNECTED_LOOP_DELAY_MS     10
#define AGENT_HEALTH_INTERVAL_MS    1000
#define HEARTBEAT_INTERVAL_MS       1000


static const char *TAG = "micro_ros";


typedef enum {
    WAITING_AGENT = 0,
    AGENT_AVAILABLE,
    AGENT_CONNECTED,
    AGENT_DISCONNECTED
} micro_ros_state_t;


/*
 * ROS objects are kept at file scope because they must survive
 * across state-machine iterations and be accessible by both
 * create_entities() and destroy_entities().
 */
static rcl_allocator_t allocator;

static rcl_init_options_t init_options;
static rclc_support_t support;
static rcl_node_t node;

static rcl_publisher_t heartbeat_pub;
static rcl_subscription_t command_sub;
static rclc_executor_t executor;

static std_msgs__msg__Int32 heartbeat_msg;
static std_msgs__msg__Int32 command_msg;

static bool init_options_initialized = false;
static bool support_initialized = false;
static bool node_initialized = false;
static bool publisher_initialized = false;
static bool subscription_initialized = false;
static bool executor_initialized = false;


/*
 * Reset all ROS handles to their zero-initialized state.
 */
static void reset_ros_handles(void)
{
    init_options =
        rcl_get_zero_initialized_init_options();

    support = (rclc_support_t){0};

    node =
        rcl_get_zero_initialized_node();

    heartbeat_pub =
        rcl_get_zero_initialized_publisher();

    command_sub =
        rcl_get_zero_initialized_subscription();

    executor =
        rclc_executor_get_zero_initialized_executor();

    init_options_initialized = false;
    support_initialized = false;
    node_initialized = false;
    publisher_initialized = false;
    subscription_initialized = false;
    executor_initialized = false;
}


/*
 * Incoming command callback.
 */
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


/*
 * Configure an init-options object with the FCN agent address.
 *
 * We create this object before each connection attempt so that a
 * complete ROS session can be destroyed and rebuilt cleanly.
 */
static bool configure_init_options(void)
{
    rcl_ret_t rc;

    init_options =
        rcl_get_zero_initialized_init_options();

    rc = rcl_init_options_init(
        &init_options,
        allocator
    );

    if (rc != RCL_RET_OK) {
        ESP_LOGE(
            TAG,
            "rcl_init_options_init failed: %d",
            (int)rc
        );
        return false;
    }

    init_options_initialized = true;

    rmw_init_options_t *rmw_options =
        rcl_init_options_get_rmw_init_options(
            &init_options
        );

    if (rmw_options == NULL) {
        ESP_LOGE(
            TAG,
            "Unable to get RMW init options"
        );
        return false;
    }

    rmw_ret_t rmw_rc =
        rmw_uros_options_set_udp_address(
            AGENT_IP,
            AGENT_PORT,
            rmw_options
        );

    if (rmw_rc != RMW_RET_OK) {
        ESP_LOGE(
            TAG,
            "Failed to set agent address: %d",
            (int)rmw_rc
        );
        return false;
    }

    return true;
}


/*
 * Ping the agent using the currently configured init options.
 */
static bool ping_agent(void)
{
    if (!init_options_initialized) {
        return false;
    }

    rmw_init_options_t *rmw_options =
        rcl_init_options_get_rmw_init_options(
            &init_options
        );

    if (rmw_options == NULL) {
        return false;
    }

    rmw_ret_t rmw_rc =
        rmw_uros_ping_agent_options(
            AGENT_PING_TIMEOUT_MS,
            AGENT_PING_ATTEMPTS,
            rmw_options
        );

    return (rmw_rc == RMW_RET_OK);
}


/*
 * Create the ROS node, publisher, subscriber and executor.
 *
 * init_options must already be configured and the agent must
 * already have answered a ping.
 */
static bool create_entities(void)
{
    rcl_ret_t rc;

    ESP_LOGI(TAG, "Creating ROS entities...");

    support = (rclc_support_t){0};

    rc = rclc_support_init_with_options(
        &support,
        0,
        NULL,
        &init_options,
        &allocator
    );

    if (rc != RCL_RET_OK) {
        ESP_LOGE(
            TAG,
            "rclc_support_init_with_options failed: %d",
            (int)rc
        );
        return false;
    }

    support_initialized = true;


    /*
     * Node
     */
    node =
        rcl_get_zero_initialized_node();

    rc = rclc_node_init_default(
        &node,
        "esp32_camera_node",
        "",
        &support
    );

    if (rc != RCL_RET_OK) {
        ESP_LOGE(
            TAG,
            "rclc_node_init_default failed: %d",
            (int)rc
        );
        return false;
    }

    node_initialized = true;


    /*
     * Heartbeat publisher
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
        ESP_LOGE(
            TAG,
            "heartbeat publisher init failed: %d",
            (int)rc
        );
        return false;
    }

    publisher_initialized = true;


    /*
     * Command subscriber
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
        ESP_LOGE(
            TAG,
            "command subscriber init failed: %d",
            (int)rc
        );
        return false;
    }

    subscription_initialized = true;


    /*
     * Executor
     */
    executor =
        rclc_executor_get_zero_initialized_executor();

    rc = rclc_executor_init(
        &executor,
        &support.context,
        1,
        &allocator
    );

    if (rc != RCL_RET_OK) {
        ESP_LOGE(
            TAG,
            "executor init failed: %d",
            (int)rc
        );
        return false;
    }

    executor_initialized = true;


    rc = rclc_executor_add_subscription(
        &executor,
        &command_sub,
        &command_msg,
        &command_callback,
        ON_NEW_DATA
    );

    if (rc != RCL_RET_OK) {
        ESP_LOGE(
            TAG,
            "executor add subscription failed: %d",
            (int)rc
        );
        return false;
    }


    heartbeat_msg.data = 0;

    ESP_LOGI(TAG, "ROS entities created");
    ESP_LOGI(TAG, "Publisher : %s", HEARTBEAT_TOPIC);
    ESP_LOGI(TAG, "Subscriber: %s", COMMAND_TOPIC);

    return true;
}


/*
 * Destroy all ROS entities that were successfully created.
 *
 * The entity-destroy session timeout is set to zero before
 * cleanup when a support context exists. This prevents cleanup
 * from waiting for an agent that may no longer be reachable.
 */
static void destroy_entities(void)
{
    rcl_ret_t rc;

    ESP_LOGI(TAG, "Destroying ROS entities...");

    if (support_initialized) {

        rmw_context_t *rmw_context =
            rcl_context_get_rmw_context(
                &support.context
            );

        if (rmw_context != NULL) {
            rmw_ret_t rmw_rc =
                rmw_uros_set_context_entity_destroy_session_timeout(
                    rmw_context,
                    0
                );

            if (rmw_rc != RMW_RET_OK) {
                ESP_LOGW(
                    TAG,
                    "Failed to set destroy timeout: %d",
                    (int)rmw_rc
                );
            }
        }
    }


    if (executor_initialized) {
        rc = rclc_executor_fini(&executor);

        if (rc != RCL_RET_OK) {
            ESP_LOGW(
                TAG,
                "executor fini failed: %d",
                (int)rc
            );
        }

        executor_initialized = false;
    }


    if (subscription_initialized &&
        node_initialized) {

        rc = rcl_subscription_fini(
            &command_sub,
            &node
        );

        if (rc != RCL_RET_OK) {
            ESP_LOGW(
                TAG,
                "subscription fini failed: %d",
                (int)rc
            );
        }

        subscription_initialized = false;
    }


    if (publisher_initialized &&
        node_initialized) {

        rc = rcl_publisher_fini(
            &heartbeat_pub,
            &node
        );

        if (rc != RCL_RET_OK) {
            ESP_LOGW(
                TAG,
                "publisher fini failed: %d",
                (int)rc
            );
        }

        publisher_initialized = false;
    }


    if (node_initialized) {
        rc = rcl_node_fini(&node);

        if (rc != RCL_RET_OK) {
            ESP_LOGW(
                TAG,
                "node fini failed: %d",
                (int)rc
            );
        }

        node_initialized = false;
    }


    if (support_initialized) {
        rc = rclc_support_fini(&support);

        if (rc != RCL_RET_OK) {
            ESP_LOGW(
                TAG,
                "support fini failed: %d",
                (int)rc
            );
        }

        support_initialized = false;
    }


    if (init_options_initialized) {
        rc = rcl_init_options_fini(
            &init_options
        );

        if (rc != RCL_RET_OK) {
            ESP_LOGW(
                TAG,
                "init options fini failed: %d",
                (int)rc
            );
        }

        init_options_initialized = false;
    }


    reset_ros_handles();

    ESP_LOGI(TAG, "ROS entities destroyed");
}


/*
 * Main micro-ROS state machine.
 */
static void micro_ros_task(void *arg)
{
    (void)arg;

    ESP_LOGI(TAG, "micro-ROS task started");

    ESP_LOGI(
        TAG,
        "Agent configured: %s:%s",
        AGENT_IP,
        AGENT_PORT
    );

    allocator =
        rcl_get_default_allocator();

    reset_ros_handles();

    micro_ros_state_t state =
        WAITING_AGENT;

    unsigned int consecutive_failures = 0;

    TickType_t last_heartbeat =
        xTaskGetTickCount();

    TickType_t last_health_check =
        xTaskGetTickCount();


    while (1) {

        switch (state) {

            /*
             * No active ROS session.
             *
             * Create/configure init options if necessary,
             * then ping the agent.
             */
            case WAITING_AGENT:

                if (!init_options_initialized) {

                    if (!configure_init_options()) {

                        /*
                         * Clean up a partially initialized
                         * init-options object before retrying.
                         */
                        destroy_entities();

                        vTaskDelay(
                            pdMS_TO_TICKS(
                                WAITING_AGENT_DELAY_MS
                            )
                        );

                        break;
                    }
                }


                if (ping_agent()) {

                    ESP_LOGI(
                        TAG,
                        "Agent detected"
                    );

                    state =
                        AGENT_AVAILABLE;
                }
                else {

                    ESP_LOGI(
                        TAG,
                        "Waiting for agent..."
                    );

                    vTaskDelay(
                        pdMS_TO_TICKS(
                            WAITING_AGENT_DELAY_MS
                        )
                    );
                }

                break;


            /*
             * Agent answered our ping.
             * Attempt to create the complete ROS session.
             */
            case AGENT_AVAILABLE:

                if (create_entities()) {

                    consecutive_failures = 0;

                    last_heartbeat =
                        xTaskGetTickCount();

                    last_health_check =
                        xTaskGetTickCount();

                    ESP_LOGI(
                        TAG,
                        "State -> AGENT_CONNECTED"
                    );

                    state =
                        AGENT_CONNECTED;
                }
                else {

                    ESP_LOGW(
                        TAG,
                        "ROS entity creation failed"
                    );

                    destroy_entities();

                    ESP_LOGI(
                        TAG,
                        "State -> WAITING_AGENT"
                    );

                    state =
                        WAITING_AGENT;

                    vTaskDelay(
                        pdMS_TO_TICKS(
                            WAITING_AGENT_DELAY_MS
                        )
                    );
                }

                break;


            /*
             * Normal operating state.
             */
            case AGENT_CONNECTED:
            {
                /*
                 * Service incoming subscription data.
                 */
                rcl_ret_t spin_rc =
                    rclc_executor_spin_some(
                        &executor,
                        RCL_MS_TO_NS(10)
                    );

                if (spin_rc != RCL_RET_OK &&
                    spin_rc != RCL_RET_TIMEOUT) {

                    ESP_LOGW(
                        TAG,
                        "Executor spin returned: %d",
                        (int)spin_rc
                    );
                }


                TickType_t now =
                    xTaskGetTickCount();


                /*
                 * Publish heartbeat once per second.
                 */
                if (
                    now - last_heartbeat >=
                    pdMS_TO_TICKS(
                        HEARTBEAT_INTERVAL_MS
                    )
                ) {

                    heartbeat_msg.data++;

                    rcl_ret_t publish_rc =
                        rcl_publish(
                            &heartbeat_pub,
                            &heartbeat_msg,
                            NULL
                        );

                    if (publish_rc == RCL_RET_OK) {

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
                            (int)publish_rc
                        );
                    }

                    last_heartbeat = now;
                }


                /*
                 * Health-check the agent once per second.
                 *
                 * A single missed UDP ping is ignored.
                 * Three consecutive failed health checks
                 * cause the session to be rebuilt.
                 */
                if (
                    now - last_health_check >=
                    pdMS_TO_TICKS(
                        AGENT_HEALTH_INTERVAL_MS
                    )
                ) {

                    if (ping_agent()) {

                        if (consecutive_failures > 0) {

                            ESP_LOGI(
                                TAG,
                                "Agent health restored"
                            );
                        }

                        consecutive_failures = 0;
                    }
                    else {

                        consecutive_failures++;

                        ESP_LOGW(
                            TAG,
                            "Agent health check failed (%u/%u)",
                            consecutive_failures,
                            AGENT_FAILURE_LIMIT
                        );

                        if (
                            consecutive_failures >=
                            AGENT_FAILURE_LIMIT
                        ) {

                            ESP_LOGW(
                                TAG,
                                "Agent connection lost"
                            );

                            state =
                                AGENT_DISCONNECTED;
                        }
                    }

                    last_health_check = now;
                }


                vTaskDelay(
                    pdMS_TO_TICKS(
                        CONNECTED_LOOP_DELAY_MS
                    )
                );

                break;
            }


            /*
             * Agent has disappeared.
             *
             * Destroy only the ROS session. Camera, Wi-Fi
             * and HTTP servers belong to other tasks and
             * are deliberately untouched.
             */
            case AGENT_DISCONNECTED:

                ESP_LOGI(
                    TAG,
                    "State -> AGENT_DISCONNECTED"
                );

                destroy_entities();

                consecutive_failures = 0;

                ESP_LOGI(
                    TAG,
                    "State -> WAITING_AGENT"
                );

                state =
                    WAITING_AGENT;

                break;


            default:

                ESP_LOGE(
                    TAG,
                    "Invalid micro-ROS state"
                );

                destroy_entities();

                state =
                    WAITING_AGENT;

                break;
        }
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