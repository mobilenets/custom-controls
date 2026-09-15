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
#include <rmw_microros/rmw_microros.h>

#include "fcn_status.h"

#define AGENT_PING_TIMEOUT_MS       250
#define AGENT_PING_ATTEMPTS         3
#define AGENT_FAILURE_LIMIT         3

#define WAITING_AGENT_DELAY_MS      1000
#define CONNECTED_LOOP_DELAY_MS     100
#define AGENT_HEALTH_INTERVAL_MS    1000


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

static bool init_options_initialized = false;
static bool support_initialized = false;
static bool node_initialized = false;

static volatile bool agent_connected = false;

static char agent_ip[16];
static char agent_port[6];
static char node_name[32];


static void reset_ros_handles(void)
{
    init_options =
        rcl_get_zero_initialized_init_options();

    support = (rclc_support_t){0};

    node =
        rcl_get_zero_initialized_node();

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
