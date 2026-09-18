
idf_component_register(
    SRCS
        "main.c"
        "../boards/waveshare_s3_8di_8ro/relay_driver.c"
        "../boards/waveshare_s3_8di_8ro/rs485_relay_driver.c"
        "../boards/waveshare_s3_8di_8ro/input_driver.c"
        "../boards/waveshare_s3_8di_8ro/network_driver.c"
        

    INCLUDE_DIRS
        "."
        "../boards/waveshare_s3_8di_8ro"

    REQUIRES
        fcn_config
        fcn_io
        fcn_network
        fcn_microros
        fcn_status
        esp_driver_i2c
        esp_driver_gpio
        esp_driver_uart
        esp_driver_spi
        esp_timer
        esp_eth
        esp_netif
        esp_event
        spi_flash
        esp_wifi
        
)