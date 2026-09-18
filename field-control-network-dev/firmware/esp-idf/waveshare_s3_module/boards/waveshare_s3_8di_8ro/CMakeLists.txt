idf_component_register(
    SRCS
        "status_led_driver.c"

    INCLUDE_DIRS
        "."

    REQUIRES
        fcn_status
        led_strip

    WHOLE_ARCHIVE
)