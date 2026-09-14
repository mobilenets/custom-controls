
idf_component_register(
    SRCS
        "main.c"
        "../boards/waveshare_s3_8di_8ro/relay_driver.c"

    INCLUDE_DIRS
        "."
        "../boards/waveshare_s3_8di_8ro"

    REQUIRES
        fcn_config
        fcn_io
        esp_driver_i2c
        spi_flash
)