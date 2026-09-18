idf_component_register(
    SRCS
        "status_driver.c"

    INCLUDE_DIRS
        "."

    REQUIRES
        fcn_status

    WHOLE_ARCHIVE
)