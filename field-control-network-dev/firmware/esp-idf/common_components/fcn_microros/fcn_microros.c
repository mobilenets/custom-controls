#define __STDC_WANT_LIB_EXT1__ 0

#include "fcn_microros.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "fcn_network.h"

#include <rcl/rcl.h>
#include <rclc/rclc.h>
#include <rclc/executor.h>
#include <rmw_microros/rmw_microros.h>

#include <std_msgs/msg/string.h>


#include "fcn_relay.h"
#include "fcn_input.h"

#include "fcn_status.h"

#define AGENT_PING_TIMEOUT_MS       250
#define AGENT_PING_ATTEMPTS         3
#define AGENT_FAILURE_LIMIT         3

#define WAITING_AGENT_DELAY_MS      1000
#define CONNECTED_LOOP_DELAY_MS     100
#define AGENT_HEALTH_INTERVAL_MS    1000

#define ACTIONREQUEST_BUFFER_SIZE 64

static const char *TAG = "fcn_microros";


typedef enum
{
    WAITING_AGENT = 0,
    AGENT_AVAILABLE,
    AGENT_CONNECTED,
    AGENT_DISCONNECTED

} fcn_microros_state_t;


static rcl_allocator_t allocator;
static rcl_init_options_t init_options;
static rclc_support_t support;
static rcl_node_t node;

static rcl_subscription_t actionrequest_subscriber;
static rclc_executor_t executor;

static std_msgs__msg__String actionrequest_msg;

static rcl_publisher_t modulereturn_publisher;
static std_msgs__msg__String modulereturn_msg;

#define MODULERETURN_BUFFER_SIZE 128
static char modulereturn_buffer[MODULERETURN_BUFFER_SIZE];

static bool init_options_initialized = false;
static bool support_initialized = false;
static bool node_initialized = false;

static bool actionrequest_initialized = false;
static bool executor_initialized = false;

static bool modulereturn_initialized = false;

static volatile bool agent_connected = false;

static char agent_ip[16];
static char agent_port[6];
static char node_name[32];

static char actionrequest_buffer[ACTIONREQUEST_BUFFER_SIZE];

static uint8_t local_module_id = 0;
static uint8_t local_input_count = 0;
static uint8_t local_output_count = 0;

static void reset_ros_handles(void)
{
    init_options =
        rcl_get_zero_initialized_init_options();

    support = (rclc_support_t){0};

    node =
        rcl_get_zero_initialized_node();

    actionrequest_subscriber =
        rcl_get_zero_initialized_subscription();
    
    modulereturn_publisher =
        rcl_get_zero_initialized_publisher();

    executor =
        rclc_executor_get_zero_initialized_executor();

    actionrequest_initialized = false;
    modulereturn_initialized = false;
    executor_initialized = false;

    init_options_initialized = false;
    support_initialized = false;
    node_initialized = false;
}


static bool configure_init_options(void)
{
    rcl_ret_t rc;

    init_options =
        rcl_get_zero_initialized_init_options();

    rc = rcl_init_options_init(
        &init_options,
        allocator
    );

    if (rc != RCL_RET_OK)
    {
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

    if (rmw_options == NULL)
    {
        ESP_LOGE(
            TAG,
            "Unable to get RMW init options"
        );

        return false;
    }

    rmw_ret_t rmw_rc =
        rmw_uros_options_set_udp_address(
            agent_ip,
            agent_port,
            rmw_options
        );

    if (rmw_rc != RMW_RET_OK)
    {
        ESP_LOGE(
            TAG,
            "Failed to set agent address: %d",
            (int)rmw_rc
        );

        return false;
    }

    return true;
}


static bool ping_agent(void)
{
    if (!init_options_initialized)
    {
        return false;
    }

    rmw_init_options_t *rmw_options =
        rcl_init_options_get_rmw_init_options(
            &init_options
        );

    if (rmw_options == NULL)
    {
        return false;
    }

    return (
        rmw_uros_ping_agent_options(
            AGENT_PING_TIMEOUT_MS,
            AGENT_PING_ATTEMPTS,
            rmw_options
        ) == RMW_RET_OK
    );
}

static void actionrequest_callback(const void *msgin)
{
    
    const std_msgs__msg__String *msg =
        (const std_msgs__msg__String *)msgin;

    if (msg == NULL || msg->data.data == NULL)
    {
        ESP_LOGW(TAG, "Empty /actionrequest received");
        return;
    }

    ESP_LOGI(
            TAG,
            "Received /actionrequest: '%s'",
            msg->data.data
        );

    unsigned int module_id = 0;
    unsigned int relay_number = 0;
    char command[8] = {0};
    char extra = '\0';

    int fields = sscanf(
        msg->data.data,
        "%u;%u;%7[^;];%c",
        &module_id,
        &relay_number,
        command,
        &extra
    );

    if (fields != 3)
    {
        ESP_LOGW(
            TAG,
            "Invalid /actionrequest: '%s'",
            msg->data.data
        );

        return;
    }

    if (module_id != local_module_id)
    {
        ESP_LOGD(
            TAG,
            "Ignoring command for module %u",
            module_id
        );

        return;
    }

    if (
        relay_number < 1 ||
        relay_number > local_output_count
    )
    {
        ESP_LOGW(
            TAG,
            "Invalid relay number: %u "
            "(configured outputs: %u)",
            relay_number,
            local_output_count
        );

        return;
    }

    bool requested_state;

    if (strcmp(command, "ON") == 0)
    {
        requested_state = true;
    }
    else if (strcmp(command, "OFF") == 0)
    {
        requested_state = false;
    }
    else
    {
        ESP_LOGW(
            TAG,
            "Invalid relay command: '%s'",
            command
        );

        return;
    }

    esp_err_t err =
        fcn_relay_set(
            (uint8_t)relay_number,
            requested_state
        );

    if (err != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "Relay %u command failed: %s",
            relay_number,
            esp_err_to_name(err)
        );

        return;
    }

    ESP_LOGI(
        TAG,
        "Action accepted: module=%u relay=%u state=%s",
        module_id,
        relay_number,
        requested_state ? "ON" : "OFF"
    );
}

static void publish_modulereturn(void)
{
    if (!modulereturn_initialized)
    {
        return;
    }

    /*
     * Refresh physical relay state once before
     * constructing the heartbeat.
     *
     * Board/provider code decides how that state
     * is obtained.
     */
    esp_err_t refresh_err =
        fcn_relay_refresh();

    if (refresh_err != ESP_OK)
    {
        ESP_LOGW(
            TAG,
            "Relay state refresh failed: %s",
            esp_err_to_name(refresh_err)
        );
    }

    uint8_t input_mask =
        fcn_input_get_mask();

    size_t used = 0;

    /*
     * Module ID and beginning of relay list.
     */
    int written = snprintf(
        modulereturn_buffer,
        sizeof(modulereturn_buffer),
        "%u;R:",
        local_module_id
    );

    if (
        written < 0 ||
        (size_t)written >= sizeof(modulereturn_buffer)
    )
    {
        ESP_LOGE(
            TAG,
            "/modulereturn buffer overflow"
        );

        return;
    }

    used = (size_t)written;

    /*
     * Relay/output states are dynamic according
     * to the configured FCN output count.
     */
    for (
        uint8_t relay_number = 1;
        relay_number <= local_output_count;
        relay_number++
    )
    {
        int state =
            fcn_relay_get(relay_number);

        if (state < 0)
        {
            ESP_LOGW(
                TAG,
                "Unable to read relay %u",
                relay_number
            );

            state = 0;
        }

        written = snprintf(
            modulereturn_buffer + used,
            sizeof(modulereturn_buffer) - used,
            "%s%d",
            relay_number > 1 ? "," : "",
            state
        );

        if (
            written < 0 ||
            (size_t)written >=
                (sizeof(modulereturn_buffer) - used)
        )
        {
            ESP_LOGE(
                TAG,
                "/modulereturn buffer overflow"
            );

            return;
        }

        used += (size_t)written;
    }

    /*
    * Input states are dynamic according
    * to the configured FCN input count.
    */
    written = snprintf(
        modulereturn_buffer + used,
        sizeof(modulereturn_buffer) - used,
        ";I:"
    );

    if (
        written < 0 ||
        (size_t)written >=
            (sizeof(modulereturn_buffer) - used)
    )
    {
        ESP_LOGE(
            TAG,
            "/modulereturn buffer overflow"
        );

        return;
    }

    used += (size_t)written;

    for (
        uint8_t input_number = 1;
        input_number <= local_input_count;
        input_number++
    )
    {
        int state =
            (input_mask >> (input_number - 1)) & 0x01;

        written = snprintf(
            modulereturn_buffer + used,
            sizeof(modulereturn_buffer) - used,
            "%s%d",
            input_number > 1 ? "," : "",
            state
        );

        if (
            written < 0 ||
            (size_t)written >=
                (sizeof(modulereturn_buffer) - used)
        )
        {
            ESP_LOGE(
                TAG,
                "/modulereturn buffer overflow"
            );

            return;
        }

        used += (size_t)written;
    }

    modulereturn_msg.data.size =
        used;

    rcl_ret_t rc =
        rcl_publish(
            &modulereturn_publisher,
            &modulereturn_msg,
            NULL
        );

    if (rc != RCL_RET_OK)
    {
        ESP_LOGW(
            TAG,
            "/modulereturn publish failed: %d",
            (int)rc
        );
    }
}

static bool create_entities(void)
{
    rcl_ret_t rc;

    ESP_LOGI(TAG, "Creating ROS session...");

    support = (rclc_support_t){0};

    rc = rclc_support_init_with_options(
        &support,
        0,
        NULL,
        &init_options,
        &allocator
    );

    if (rc != RCL_RET_OK)
    {
        ESP_LOGE(
            TAG,
            "rclc_support_init_with_options failed: %d",
            (int)rc
        );

        return false;
    }

    support_initialized = true;

    node =
        rcl_get_zero_initialized_node();

    rc = rclc_node_init_default(
        &node,
        node_name,
        "",
        &support
    );

    if (rc != RCL_RET_OK)
    {
        ESP_LOGE(
            TAG,
            "rclc_node_init_default failed: %d",
            (int)rc
        );

        return false;
    }

    node_initialized = true;

    ESP_LOGI(
        TAG,
        "ROS node created: %s",
        node_name
    );

    actionrequest_subscriber =
        rcl_get_zero_initialized_subscription();

    rc = rclc_subscription_init_best_effort(
        &actionrequest_subscriber,
        &node,
        ROSIDL_GET_MSG_TYPE_SUPPORT(
            std_msgs,
            msg,
            String
        ),
        "/actionrequest"
    );

    if (rc != RCL_RET_OK)
    {
        ESP_LOGE(
            TAG,
            "Failed to create /actionrequest subscriber: %d",
            (int)rc
        );

        return false;
    }

    actionrequest_initialized = true;


    memset(
        &actionrequest_msg,
        0,
        sizeof(actionrequest_msg)
    );

    /*
    * Preallocate the String storage so incoming ROS messages
    * do not depend on repeated heap allocation.
    */
    actionrequest_msg.data.data =
        actionrequest_buffer;

    actionrequest_msg.data.size = 0;

    actionrequest_msg.data.capacity =
        sizeof(actionrequest_buffer);

    actionrequest_buffer[0] = '\0';


    executor =
        rclc_executor_get_zero_initialized_executor();

    rc = rclc_executor_init(
        &executor,
        &support.context,
        1,
        &allocator
    );

    if (rc != RCL_RET_OK)
    {
        ESP_LOGE(
            TAG,
            "Failed to initialize executor: %d",
            (int)rc
        );

        return false;
    }

    executor_initialized = true;


    rc = rclc_executor_add_subscription(
        &executor,
        &actionrequest_subscriber,
        &actionrequest_msg,
        &actionrequest_callback,
        ON_NEW_DATA
    );

    if (rc != RCL_RET_OK)
    {
        ESP_LOGE(
            TAG,
            "Failed to add /actionrequest to executor: %d",
            (int)rc
        );

        return false;
    }

    ESP_LOGI(
        TAG,
        "Subscribed to /actionrequest"
    );

    modulereturn_publisher =
        rcl_get_zero_initialized_publisher();

    rc = rclc_publisher_init_best_effort(
        &modulereturn_publisher,
        &node,
        ROSIDL_GET_MSG_TYPE_SUPPORT(
            std_msgs,
            msg,
            String
        ),
        "/modulereturn"
    );

    if (rc != RCL_RET_OK)
    {
        ESP_LOGE(
            TAG,
            "Failed to create /modulereturn publisher: %d",
            (int)rc
        );

        return false;
    }

    modulereturn_initialized = true;

    memset(
        &modulereturn_msg,
        0,
        sizeof(modulereturn_msg)
    );

    modulereturn_msg.data.data =
        modulereturn_buffer;

    modulereturn_msg.data.size = 0;

    modulereturn_msg.data.capacity =
        sizeof(modulereturn_buffer);

    modulereturn_buffer[0] = '\0';

    ESP_LOGI(
        TAG,
        "Publishing /modulereturn"
    );

    return true;
}


static void destroy_entities(void)
{
    rcl_ret_t rc;

    agent_connected = false;

    esp_err_t status_err =
        fcn_status_set(FCN_STATUS_OFF);

    if (status_err != ESP_OK)
    {
        ESP_LOGW(
            TAG,
            "Failed to clear status LED: %s",
            esp_err_to_name(status_err)
        );
    }

    ESP_LOGI(TAG, "Destroying ROS session...");

    if (support_initialized)
    {
        rmw_context_t *rmw_context =
            rcl_context_get_rmw_context(
                &support.context
            );

        if (rmw_context != NULL)
        {
            rmw_uros_set_context_entity_destroy_session_timeout(
                rmw_context,
                0
            );
        }
    }

    if (executor_initialized)
    {
        rc = rclc_executor_fini(&executor);

        if (rc != RCL_RET_OK)
        {
            ESP_LOGW(
                TAG,
                "executor fini failed: %d",
                (int)rc
            );
        }

        executor_initialized = false;
    }

    if (modulereturn_initialized)
    {
        rc = rcl_publisher_fini(
            &modulereturn_publisher,
            &node
        );

        if (rc != RCL_RET_OK)
        {
            ESP_LOGW(
                TAG,
                "modulereturn publisher fini failed: %d",
                (int)rc
            );
        }

        modulereturn_initialized = false;
    }


    if (actionrequest_initialized)
    {
        rc = rcl_subscription_fini(
            &actionrequest_subscriber,
            &node
        );

        if (rc != RCL_RET_OK)
        {
            ESP_LOGW(
                TAG,
                "actionrequest subscriber fini failed: %d",
                (int)rc
            );
        }

        actionrequest_initialized = false;
    }

    if (node_initialized)
    {
        rc = rcl_node_fini(&node);

        if (rc != RCL_RET_OK)
        {
            ESP_LOGW(
                TAG,
                "node fini failed: %d",
                (int)rc
            );
        }

        node_initialized = false;
    }

    if (support_initialized)
    {
        rc = rclc_support_fini(&support);

        if (rc != RCL_RET_OK)
        {
            ESP_LOGW(
                TAG,
                "support fini failed: %d",
                (int)rc
            );
        }

        support_initialized = false;
    }

    if (init_options_initialized)
    {
        rc = rcl_init_options_fini(
            &init_options
        );

        if (rc != RCL_RET_OK)
        {
            ESP_LOGW(
                TAG,
                "init options fini failed: %d",
                (int)rc
            );
        }

        init_options_initialized = false;
    }

    reset_ros_handles();

    ESP_LOGI(TAG, "ROS session destroyed");
}


static void micro_ros_task(void *arg)
{
    (void)arg;

    allocator =
        rcl_get_default_allocator();

    reset_ros_handles();

    fcn_microros_state_t state =
        WAITING_AGENT;

    unsigned int consecutive_failures = 0;

    TickType_t last_health_check =
        xTaskGetTickCount();

    TickType_t last_modulereturn =
    xTaskGetTickCount();

    ESP_LOGI(
        TAG,
        "micro-ROS task started"
    );

    ESP_LOGI(
        TAG,
        "Agent configured: %s:%s",
        agent_ip,
        agent_port
    );


    while (1)
    {
        switch (state)
        {
            case WAITING_AGENT:

                agent_connected = false;

                if (!fcn_network_has_ip())
                {
                    vTaskDelay(
                        pdMS_TO_TICKS(
                            WAITING_AGENT_DELAY_MS
                        )
                    );

                    break;
                }

                if (!init_options_initialized)
                {
                    if (!configure_init_options())
                    {
                        destroy_entities();

                        vTaskDelay(
                            pdMS_TO_TICKS(
                                WAITING_AGENT_DELAY_MS
                            )
                        );

                        break;
                    }
                }

                if (ping_agent())
                {
                    ESP_LOGI(
                        TAG,
                        "Agent detected"
                    );

                    state =
                        AGENT_AVAILABLE;
                }
                else
                {
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


            case AGENT_AVAILABLE:

                if (create_entities())
                {
                    consecutive_failures = 0;

                    last_health_check =
                        xTaskGetTickCount();

                    last_modulereturn =
                        xTaskGetTickCount();

                    agent_connected = true;

                    fcn_status_set(FCN_STATUS_AGENT_CONNECTED);

                    ESP_LOGI(
                        TAG,
                        "State -> AGENT_CONNECTED"
                    );

                    state =
                        AGENT_CONNECTED;
                }
                else
                {
                    ESP_LOGW(
                        TAG,
                        "ROS session creation failed"
                    );

                    destroy_entities();

                    state =
                        WAITING_AGENT;

                    vTaskDelay(
                        pdMS_TO_TICKS(
                            WAITING_AGENT_DELAY_MS
                        )
                    );
                }

                break;


            case AGENT_CONNECTED:
            {
                TickType_t now =
                    xTaskGetTickCount();

                rcl_ret_t spin_rc =
                    rclc_executor_spin_some(
                        &executor,
                        RCL_MS_TO_NS(10)
                    );

                if (
                    now - last_modulereturn >=
                    pdMS_TO_TICKS(1000)
                )
                {
                    publish_modulereturn();

                    last_modulereturn = now;
                }

                if (
                    spin_rc != RCL_RET_OK &&
                    spin_rc != RCL_RET_TIMEOUT
                )
                {
                    ESP_LOGW(
                        TAG,
                        "Executor spin returned: %d",
                        (int)spin_rc
                    );
                }

                if (!fcn_network_has_ip())
                {
                    ESP_LOGW(
                        TAG,
                        "Network connection lost"
                    );

                    agent_connected = false;
                    
                    fcn_status_set(FCN_STATUS_OFF);

                    state =
                        AGENT_DISCONNECTED;

                    break;
                }

                if (
                    now - last_health_check >=
                    pdMS_TO_TICKS(
                        AGENT_HEALTH_INTERVAL_MS
                    )
                )
                {
                    if (ping_agent())
                    {
                        if (consecutive_failures > 0)
                        {
                            ESP_LOGI(
                                TAG,
                                "Agent health restored"
                            );
                        }

                        consecutive_failures = 0;
                    }
                    else
                    {
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
                        )
                        {
                            ESP_LOGW(
                                TAG,
                                "Agent connection lost"
                            );

                            agent_connected = false;

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


esp_err_t fcn_microros_start(
    const fcn_microros_config_t *config
)
{
    if (
        config == NULL ||
        config->agent_ip == NULL ||
        config->agent_ip[0] == '\0' ||
        config->agent_port == 0
    )
    {
        return ESP_ERR_INVALID_ARG;
    }

    snprintf(
        agent_ip,
        sizeof(agent_ip),
        "%s",
        config->agent_ip
    );

    snprintf(
        agent_port,
        sizeof(agent_port),
        "%u",
        config->agent_port
    );

    snprintf(
        node_name,
        sizeof(node_name),
        "fcn_module_%u",
        config->module_id
    );

    local_module_id =
        config->module_id;

    local_input_count =
        config->input_count;

    local_output_count =
        config->output_count;

    BaseType_t task_result =
        xTaskCreate(
            micro_ros_task,
            "fcn_microros",
            12000,
            NULL,
            5,
            NULL
        );

    if (task_result != pdPASS)
    {
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}


bool fcn_microros_agent_connected(void)
{
    return agent_connected;
}
