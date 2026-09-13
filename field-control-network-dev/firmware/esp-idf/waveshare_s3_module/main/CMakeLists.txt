idf_component_register(
    SRCS
        "main.c"

    INCLUDE_DIRS
        "."
        "../boards/waveshare_s3_8di_8ro"

    REQUIRES
        fcn_config
        spi_flash
)
