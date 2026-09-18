idf_component_register(
    SRCS
        "fcn_microros.c"

    INCLUDE_DIRS
        "include"

    REQUIRES
        fcn_network
        fcn_status
        fcn_io
        micro_ros_espidf_component
)
