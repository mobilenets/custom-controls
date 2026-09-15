idf_component_register(
    SRCS
        "fcn_status.c"
        "../../boards/waveshare_s3_8di_8ro/status_led_driver.c"

    INCLUDE_DIRS
        "include"
        "../../boards/waveshare_s3_8di_8ro"

    REQUIRES
        led_strip
)