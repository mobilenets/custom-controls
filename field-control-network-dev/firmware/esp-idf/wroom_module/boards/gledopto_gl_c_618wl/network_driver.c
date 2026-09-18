#include "fcn_network.h"

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_ip_addr.h"
#include "esp_wifi.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "network_driver";

static esp_netif_t *wifi_netif = NULL;

static bool wifi_started = false;
static bool wifi_has_ip = false;

static char s_ip_string[16] = "0.0.0.0";
static char s_gateway_string[16] = "0.0.0.0";
static char s_netmask_string[16] = "0.0.0.0";


static void wifi_event_handler(
    void *arg,
    esp_event_base_t event_base,
    int32_t event_id,
    void *event_data)
{
    switch (event_id)
    {
        case WIFI_EVENT_STA_START:
            ESP_LOGI(TAG, "Wi-Fi station started");
            esp_wifi_connect();
            break;

        case WIFI_EVENT_STA_CONNECTED:
            ESP_LOGI(TAG, "Wi-Fi connected to AP");
            break;

        case WIFI_EVENT_STA_DISCONNECTED:
            wifi_has_ip = false;

            strcpy(s_ip_string, "0.0.0.0");
            strcpy(s_gateway_string, "0.0.0.0");
            strcpy(s_netmask_string, "0.0.0.0");

            ESP_LOGW(TAG, "Wi-Fi disconnected");
            ESP_LOGI(TAG, "Retrying Wi-Fi");

            esp_wifi_connect();
            break;

        default:
            break;
    }
}


static void wifi_got_ip_handler(
    void *arg,
    esp_event_base_t event_base,
    int32_t event_id,
    void *event_data)
{
    ip_event_got_ip_t *event =
        (ip_event_got_ip_t *)event_data;

    wifi_has_ip = true;

    snprintf(
        s_ip_string,
        sizeof(s_ip_string),
        IPSTR,
        IP2STR(&event->ip_info.ip)
    );

    snprintf(
        s_gateway_string,
        sizeof(s_gateway_string),
        IPSTR,
        IP2STR(&event->ip_info.gw)
    );

    snprintf(
        s_netmask_string,
        sizeof(s_netmask_string),
        IPSTR,
        IP2STR(&event->ip_info.netmask)
    );

    ESP_LOGI(TAG, "Wi-Fi DHCP lease acquired");
    ESP_LOGI(TAG, "IP address: %s", s_ip_string);
    ESP_LOGI(TAG, "Gateway:   %s", s_gateway_string);
    ESP_LOGI(TAG, "Netmask:   %s", s_netmask_string);
}


esp_err_t fcn_network_init(
    const fcn_network_config_t *config)
{
    if (config == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (config->wifi_ssid == NULL ||
        config->wifi_ssid[0] == '\0')
    {
        ESP_LOGE(TAG, "Wi-Fi SSID is empty");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Initializing Wi-Fi");
    ESP_LOGI(TAG, "SSID: %s", config->wifi_ssid);

    ESP_RETURN_ON_ERROR(
        esp_netif_init(),
        TAG,
        "esp_netif_init failed"
    );

    esp_err_t err = esp_event_loop_create_default();

    if (err != ESP_OK &&
        err != ESP_ERR_INVALID_STATE)
    {
        ESP_LOGE(
            TAG,
            "Default event loop creation failed: %s",
            esp_err_to_name(err)
        );

        return err;
    }

    wifi_netif = esp_netif_create_default_wifi_sta();

    if (wifi_netif == NULL)
    {
        ESP_LOGE(TAG, "Failed to create Wi-Fi netif");
        return ESP_FAIL;
    }

    wifi_init_config_t wifi_init_config =
        WIFI_INIT_CONFIG_DEFAULT();

    ESP_RETURN_ON_ERROR(
        esp_wifi_init(&wifi_init_config),
        TAG,
        "esp_wifi_init failed"
    );

    ESP_RETURN_ON_ERROR(
        esp_event_handler_register(
            WIFI_EVENT,
            ESP_EVENT_ANY_ID,
            &wifi_event_handler,
            NULL
        ),
        TAG,
        "Wi-Fi event registration failed"
    );

    ESP_RETURN_ON_ERROR(
        esp_event_handler_register(
            IP_EVENT,
            IP_EVENT_STA_GOT_IP,
            &wifi_got_ip_handler,
            NULL
        ),
        TAG,
        "Wi-Fi IP event registration failed"
    );

    wifi_config_t wifi_config = {0};

    strlcpy(
        (char *)wifi_config.sta.ssid,
        config->wifi_ssid,
        sizeof(wifi_config.sta.ssid)
    );

    if (config->wifi_password != NULL)
    {
        strlcpy(
            (char *)wifi_config.sta.password,
            config->wifi_password,
            sizeof(wifi_config.sta.password)
        );
    }

    ESP_RETURN_ON_ERROR(
        esp_wifi_set_mode(WIFI_MODE_STA),
        TAG,
        "esp_wifi_set_mode failed"
    );

    ESP_RETURN_ON_ERROR(
        esp_wifi_set_config(
            WIFI_IF_STA,
            &wifi_config
        ),
        TAG,
        "esp_wifi_set_config failed"
    );

    ESP_RETURN_ON_ERROR(
        esp_wifi_start(),
        TAG,
        "esp_wifi_start failed"
    );

    wifi_started = true;

    ESP_LOGI(TAG, "Wi-Fi initialized");

    return ESP_OK;
}


bool fcn_network_link_up(void)
{
    return wifi_started;
}


bool fcn_network_has_ip(void)
{
    return wifi_has_ip;
}


const char *fcn_network_get_ip_string(void)
{
    return s_ip_string;
}


const char *fcn_network_get_gateway_string(void)
{
    return s_gateway_string;
}


const char *fcn_network_get_netmask_string(void)
{
    return s_netmask_string;
}