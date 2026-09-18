idf_component_register(
    SRCS
        "fcn_config.c"
        "fcn_config_console.c"
        "fcn_config_boot.c"

    INCLUDE_DIRS
        "include"

    REQUIRES
        nvs_flash
)