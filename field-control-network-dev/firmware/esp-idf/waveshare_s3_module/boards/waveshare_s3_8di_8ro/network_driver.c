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

static const char *TAG = "network_driver";

static esp_eth_handle_t eth_handle = NULL;
static esp_netif_t *eth_netif = NULL;

static bool ethernet_link_up = false;
static bool ethernet_has_ip = false;


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
            ESP_LOGW(TAG, "Ethernet link DOWN");
            break;

        case ETHERNET_EVENT_START:
            ESP_LOGI(TAG, "Ethernet started");
            break;

        case ETHERNET_EVENT_STOP:
            ethernet_link_up = false;
            ethernet_has_ip = false;
            ESP_LOGI(TAG, "Ethernet stopped");
            break;

        default:
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

    ethernet_has_ip = true;

    ESP_LOGI(TAG, "Ethernet acquired IP address");

    ESP_LOGI(
        TAG,
        "IP:      " IPSTR,
        IP2STR(&ip_info->ip)
    );

    ESP_LOGI(
        TAG,
        "Netmask: " IPSTR,
        IP2STR(&ip_info->netmask)
    );

    ESP_LOGI(
        TAG,
        "Gateway: " IPSTR,
        IP2STR(&ip_info->gw)
    );
}


esp_err_t fcn_network_init(void)
{
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


    err = gpio_install_isr_service(0);

    if (err != ESP_OK &&
        err != ESP_ERR_INVALID_STATE)
    {
        ESP_LOGE(
            TAG,
            "GPIO ISR service installation failed: %s",
            esp_err_to_name(err)
        );

        return err;
    }


    spi_bus_config_t bus_config = {
        .miso_io_num = FCN_ETH_MISO_GPIO,
        .mosi_io_num = FCN_ETH_MOSI_GPIO,
        .sclk_io_num = FCN_ETH_SCLK_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1
    };


    err = spi_bus_initialize(
        SPI2_HOST,
        &bus_config,
        SPI_DMA_CH_AUTO
    );

    if (err != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "SPI bus initialization failed: %s",
            esp_err_to_name(err)
        );

        return err;
    }


    spi_device_interface_config_t spi_device_config = {
        .mode = 0,
        .clock_speed_hz = 20 * 1000 * 1000,
        .spics_io_num = FCN_ETH_CS_GPIO,
        .queue_size = 20
    };


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

    return esp_eth_start(eth_handle);
}


bool fcn_network_link_up(void)
{
    return ethernet_link_up;
}


bool fcn_network_has_ip(void)
{
    return ethernet_has_ip;
}
