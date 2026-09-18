idf_component_register(
    SRCS
        "main.c"
        "../boards/gledopto_gl_c_618wl/output_driver.c"
        "../boards/gledopto_gl_c_618wl/network_driver.c"

    INCLUDE_DIRS
        "."
        "../boards/gledopto_gl_c_618wl"

    REQUIRES
        fcn_io
        fcn_config
        esp_driver_gpio
        spi_flash
        fcn_network
        esp_wifi
        esp_netif
)