#include "fcn_network.h"
#include "board_profile.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"

#include "esp_eth.h"
#include "esp_eth_mac.h"
#include "esp_eth_phy.h"
#include "esp_event.h"  
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_eth_mac_w5500.h"
#include "esp_eth_phy_w5500.h"
#include "esp_check.h"
#include <stdbool.h>
#include "esp_mac.h"
#include <string.h>
#include <stdio.h>
#include "esp_netif_ip_addr.h"

#include "esp_wifi.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "network_driver";

static esp_eth_handle_t eth_handle = NULL;
static esp_netif_t *eth_netif = NULL;

static bool ethernet_link_up = false;
static bool ethernet_has_ip = false;



static char s_ip_string[16] = "0.0.0.0";
static char s_gateway_string[16] = "0.0.0.0";
static char s_netmask_string[16] = "0.0.0.0";

static esp_netif_t *wifi_netif = NULL;

static bool wifi_initialized = false;
static bool wifi_started = false;
static bool wifi_has_ip = false;

static bool wifi_fallback_enabled = false;

static char wifi_ssid[33] = {0};
static char wifi_password[65] = {0};

static esp_netif_ip_info_t ethernet_ip_info = {0};
static esp_netif_ip_info_t wifi_ip_info = {0};

static void update_active_network(void);
static void network_fallback_task(void *arg);
static void wifi_event_handler(
    void *arg,
    esp_event_base_t event_base,
    int32_t event_id,
    void *event_data)
{
    switch (event_id)
    {
        case WIFI_EVENT_STA_START:

            ESP_LOGI(
                TAG,
                "Wi-Fi station started"
            );

            esp_wifi_connect();
            break;


        case WIFI_EVENT_STA_CONNECTED:

            ESP_LOGI(
                TAG,
                "Wi-Fi connected to AP"
            );

            break;


        case WIFI_EVENT_STA_DISCONNECTED:

            wifi_has_ip = false;

            memset(&wifi_ip_info, 0, sizeof(wifi_ip_info));

            ESP_LOGW(
                TAG,
                "Wi-Fi disconnected"
            );

            update_active_network();

            /*
             * Only keep trying Wi-Fi when it is
             * actually needed as fallback.
             */
            if (!ethernet_has_ip &&
                wifi_fallback_enabled)
            {
                ESP_LOGI(
                    TAG,
                    "Retrying Wi-Fi fallback"
                );

                esp_wifi_connect();
            }

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

    wifi_ip_info = event->ip_info;
    wifi_has_ip = true;

    ESP_LOGI(
        TAG,
        "Wi-Fi DHCP lease acquired"
    );

    ESP_LOGI(
        TAG,
        "Wi-Fi IP: " IPSTR,
        IP2STR(&event->ip_info.ip)
    );

    ESP_LOGI(
        TAG,
        "Wi-Fi Gateway: " IPSTR,
        IP2STR(&event->ip_info.gw)
    );

    ESP_LOGI(
        TAG,
        "Wi-Fi Netmask: " IPSTR,
        IP2STR(&event->ip_info.netmask)
    );

    update_active_network();
}

static void ethernet_event_handler(
    void *arg,
    esp_event_base_t event_base,
    int32_t event_id,
    void *event_data)
{
    switch (event_id)
    {
        case ETHERNET_EVENT_CONNECTED:
            ethernet_link_up = true;
            ESP_LOGI(TAG, "Ethernet link UP");
            break;

        case ETHERNET_EVENT_DISCONNECTED:

            ethernet_link_up = false;
            ethernet_has_ip = false;

            memset(
                &ethernet_ip_info,
                0,
                sizeof(ethernet_ip_info)
            );

            ESP_LOGW(
                TAG,
                "Ethernet link DOWN"
            );

            update_active_network();

            if (wifi_fallback_enabled &&
                wifi_initialized)
            {
                if (wifi_has_ip)
                {
                    ESP_LOGI(
                        TAG,
                        "Wi-Fi fallback already available"
                    );
                }
                else if (!wifi_started)
                {
                    ESP_LOGI(
                        TAG,
                        "Activating Wi-Fi fallback"
                    );

                    esp_err_t err = esp_wifi_start();

                    if (err == ESP_OK)
                    {
                        wifi_started = true;
                    }
                    else
                    {
                        ESP_LOGE(
                            TAG,
                            "Failed to start Wi-Fi fallback: %s",
                            esp_err_to_name(err)
                        );
                    }
                }
                else
                {
                    ESP_LOGI(
                        TAG,
                        "Reconnecting Wi-Fi fallback"
                    );

                    esp_wifi_connect();
                }
            }
            break;

        case ETHERNET_EVENT_START:
            ESP_LOGI(TAG, "Ethernet started");
            break;

        case ETHERNET_EVENT_STOP:
            ethernet_link_up = false;
            ethernet_has_ip = false;

            memset(
                &ethernet_ip_info,
                0,
                sizeof(ethernet_ip_info)
            );

            ESP_LOGI(TAG, "Ethernet stopped");

            update_active_network();
            break;
    }
}

static void got_ip_event_handler(
    void *arg,
    esp_event_base_t event_base,
    int32_t event_id,
    void *event_data)
{
    ip_event_got_ip_t *event =
        (ip_event_got_ip_t *)event_data;

    const esp_netif_ip_info_t *ip_info =
        &event->ip_info;

    snprintf(
        s_ip_string,
        sizeof(s_ip_string),
        IPSTR,
        IP2STR(&ip_info->ip)
    );

    snprintf(
        s_gateway_string,
        sizeof(s_gateway_string),
        IPSTR,
        IP2STR(&ip_info->gw)
    );

    snprintf(
        s_netmask_string,
        sizeof(s_netmask_string),
        IPSTR,
        IP2STR(&ip_info->netmask)
    );

    ethernet_ip_info = event->ip_info;
    ethernet_has_ip = true;
    update_active_network();

    ESP_LOGI(TAG, "Ethernet DHCP lease acquired");
    ESP_LOGI(TAG, "IP address: " IPSTR,
            IP2STR(&ip_info->ip));

    ESP_LOGI(TAG, "Gateway:    " IPSTR,
            IP2STR(&ip_info->gw));

    ESP_LOGI(TAG, "Netmask:    " IPSTR,
            IP2STR(&ip_info->netmask));

        
}

esp_err_t fcn_network_init(
    const fcn_network_config_t *config)
{
    if (config == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    wifi_fallback_enabled =
        config->wifi_fallback_enabled;

    if (config->wifi_ssid != NULL)
    {
        strlcpy(
            wifi_ssid,
            config->wifi_ssid,
            sizeof(wifi_ssid)
        );
    }

    if (config->wifi_password != NULL)
    {
        strlcpy(
            wifi_password,
            config->wifi_password,
            sizeof(wifi_password)
        );
    }

    ESP_LOGI(TAG, "Initializing W5500 Ethernet");

    ESP_LOGI(
        TAG,
        "SPI SCLK=%d MOSI=%d MISO=%d CS=%d IRQ=%d",
        FCN_ETH_SCLK_GPIO,
        FCN_ETH_MOSI_GPIO,
        FCN_ETH_MISO_GPIO,
        FCN_ETH_CS_GPIO,
        FCN_ETH_IRQ_GPIO
    );

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

/* -------------------------------------------------
 * Prepare Wi-Fi fallback
 * ------------------------------------------------- */

if (wifi_fallback_enabled &&
    wifi_ssid[0] != '\0')
{
    ESP_LOGI(
        TAG,
        "Preparing Wi-Fi fallback for SSID: %s",
        wifi_ssid
    );

    wifi_netif =
        esp_netif_create_default_wifi_sta();

    if (wifi_netif == NULL)
    {
        ESP_LOGE(
            TAG,
            "Failed to create Wi-Fi netif"
        );

        return ESP_FAIL;
    }

    esp_netif_set_route_prio(
        wifi_netif,
        100
    );

    wifi_init_config_t wifi_init_config =
        WIFI_INIT_CONFIG_DEFAULT();

    ESP_RETURN_ON_ERROR(
        esp_wifi_init(&wifi_init_config),
        TAG,
        "esp_wifi_init failed"
    );

    wifi_config_t wifi_config = {0};

    strlcpy(
        (char *)wifi_config.sta.ssid,
        wifi_ssid,
        sizeof(wifi_config.sta.ssid)
    );

    strlcpy(
        (char *)wifi_config.sta.password,
        wifi_password,
        sizeof(wifi_config.sta.password)
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

    wifi_initialized = true;

    ESP_LOGI(
        TAG,
        "Wi-Fi fallback initialized"
    );
}

/* -------------------------------------------------
 * Existing W5500 Ethernet initialization continues
 * below here
 * ------------------------------------------------- */

    spi_device_interface_config_t spi_device_config = {
        .mode = 0,
        .clock_speed_hz = 20 * 1000 * 1000,
        .spics_io_num = FCN_ETH_CS_GPIO,
        .queue_size = 20
    };

    spi_bus_config_t bus_config = {
        .mosi_io_num = FCN_ETH_MOSI_GPIO,
        .miso_io_num = FCN_ETH_MISO_GPIO,
        .sclk_io_num = FCN_ETH_SCLK_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };

    ESP_RETURN_ON_ERROR(
        spi_bus_initialize(
            SPI2_HOST,
            &bus_config,
            SPI_DMA_CH_AUTO
        ),
        TAG,
        "spi_bus_initialize failed"
    );    

    esp_err_t gpio_isr_err = gpio_install_isr_service(0);

    if (gpio_isr_err != ESP_OK &&
        gpio_isr_err != ESP_ERR_INVALID_STATE)
    {
        ESP_LOGE(
            TAG,
            "GPIO ISR service installation failed: %s",
            esp_err_to_name(gpio_isr_err)
        );

        return gpio_isr_err;
    }
    
    eth_w5500_config_t w5500_config =
        ETH_W5500_DEFAULT_CONFIG(
            SPI2_HOST,
            &spi_device_config
        );

    w5500_config.base.int_gpio_num =
    FCN_ETH_IRQ_GPIO;


    eth_mac_config_t mac_config =
        ETH_MAC_DEFAULT_CONFIG();


    esp_eth_mac_t *mac =
        esp_eth_mac_new_w5500(
            &w5500_config,
            &mac_config
        );

    if (mac == NULL)
    {
        ESP_LOGE(TAG, "Failed to create W5500 MAC");
        return ESP_FAIL;
    }


    eth_phy_config_t phy_config =
        ETH_PHY_DEFAULT_CONFIG();

    phy_config.phy_addr =
        FCN_ETH_PHY_ADDR;

    phy_config.reset_gpio_num = -1;


    esp_eth_phy_t *phy =
        esp_eth_phy_new_w5500(
            &phy_config
        );

    if (phy == NULL)
    {
        ESP_LOGE(TAG, "Failed to create W5500 PHY");
        return ESP_FAIL;
    }


    esp_eth_config_t eth_config =
        ETH_DEFAULT_CONFIG(mac, phy);


    err = esp_eth_driver_install(
        &eth_config,
        &eth_handle
    );

    if (err != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "Ethernet driver installation failed: %s",
            esp_err_to_name(err)
        );

        return err;
    }

    uint8_t eth_mac[6];

    err = esp_read_mac(eth_mac, ESP_MAC_ETH);

    if (err != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "Failed to obtain Ethernet MAC: %s",
            esp_err_to_name(err)
        );

        return err;
    }

    err = esp_eth_ioctl(
        eth_handle,
        ETH_CMD_S_MAC_ADDR,
        eth_mac
    );

    if (err != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "Failed to set Ethernet MAC: %s",
            esp_err_to_name(err)
        );

        return err;
    }

    ESP_LOGI(
        TAG,
        "Ethernet MAC: %02X:%02X:%02X:%02X:%02X:%02X",
        eth_mac[0],
        eth_mac[1],
        eth_mac[2],
        eth_mac[3],
        eth_mac[4],
        eth_mac[5]
    );

    esp_netif_config_t netif_config =
        ESP_NETIF_DEFAULT_ETH();

    eth_netif =
        esp_netif_new(&netif_config);

    if (eth_netif == NULL)
    {
        ESP_LOGE(TAG, "Failed to create Ethernet netif");
        return ESP_FAIL;
    }


    err = esp_netif_attach(
        eth_netif,
        esp_eth_new_netif_glue(eth_handle)
    );

    if (err != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "Failed to attach Ethernet netif: %s",
            esp_err_to_name(err)
        );

        return err;
    }


    ESP_ERROR_CHECK(
        esp_event_handler_register(
            ETH_EVENT,
            ESP_EVENT_ANY_ID,
            &ethernet_event_handler,
            NULL
        )
    );


    ESP_ERROR_CHECK(
        esp_event_handler_register(
            IP_EVENT,
            IP_EVENT_ETH_GOT_IP,
            &got_ip_event_handler,
            NULL
        )
    );


    ESP_LOGI(TAG, "Starting Ethernet");

    xTaskCreate(
    network_fallback_task,
    "network_fallback",
    3072,
    NULL,
    5,
    NULL
    );

    return esp_eth_start(eth_handle);

}

bool fcn_network_link_up(void)
{
    return ethernet_link_up;
}

bool fcn_network_has_ip(void)
{
    return ethernet_has_ip || wifi_has_ip;
}

static void network_fallback_task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(5000));

    if (!ethernet_has_ip &&
        wifi_fallback_enabled &&
        wifi_initialized &&
        !wifi_started)
    {
        ESP_LOGW(
            TAG,
            "Ethernet unavailable after startup delay"
        );

        ESP_LOGI(
            TAG,
            "Activating Wi-Fi fallback"
        );

        esp_err_t err = esp_wifi_start();

        if (err == ESP_OK)
        {
            wifi_started = true;
        }
        else
        {
            ESP_LOGE(
                TAG,
                "Failed to start Wi-Fi fallback: %s",
                esp_err_to_name(err)
            );
        }
    }

    vTaskDelete(NULL);
}

static void update_active_network(void)
{
    const esp_netif_ip_info_t *ip_info = NULL;

    /*
     * Ethernet always wins when it has an IP.
     */
    if (ethernet_has_ip)
    {
        ip_info = &ethernet_ip_info;

        ESP_LOGI(
            TAG,
            "Active interface: ETHERNET"
        );
    }
    else if (wifi_has_ip)
    {
        ip_info = &wifi_ip_info;

        ESP_LOGI(
            TAG,
            "Active interface: WI-FI"
        );
    }

    if (ip_info == NULL)
    {
        strcpy(s_ip_string, "0.0.0.0");
        strcpy(s_gateway_string, "0.0.0.0");
        strcpy(s_netmask_string, "0.0.0.0");

        return;
    }

    snprintf(
        s_ip_string,
        sizeof(s_ip_string),
        IPSTR,
        IP2STR(&ip_info->ip)
    );

    snprintf(
        s_gateway_string,
        sizeof(s_gateway_string),
        IPSTR,
        IP2STR(&ip_info->gw)
    );

    snprintf(
        s_netmask_string,
        sizeof(s_netmask_string),
        IPSTR,
        IP2STR(&ip_info->netmask)
    );

    ESP_LOGI(
        TAG,
        "Active IP: %s",
        s_ip_string
    );
}
