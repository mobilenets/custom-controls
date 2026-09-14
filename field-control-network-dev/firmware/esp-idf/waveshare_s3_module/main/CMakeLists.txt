
idf_component_register(
    SRCS
        "main.c"
        "../boards/waveshare_s3_8di_8ro/relay_driver.c"
        "../boards/waveshare_s3_8di_8ro/input_driver.c"
        "../boards/waveshare_s3_8di_8ro/network_driver.c"

    INCLUDE_DIRS
        "."
        "../boards/waveshare_s3_8di_8ro"

    REQUIRES
        fcn_config
        fcn_io
        fcn_network
        esp_driver_i2c
        esp_driver_gpio
        esp_driver_spi
        esp_eth
        esp_netif
        esp_event
        spi_flash
)