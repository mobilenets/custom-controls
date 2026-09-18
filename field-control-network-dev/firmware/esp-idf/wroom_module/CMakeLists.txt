cmake_minimum_required(VERSION 3.22)

set(EXTRA_COMPONENT_DIRS
    "../common_components"
    "boards/gledopto_gl_c_618wl"
    "../esp32_cam/components/micro_ros_espidf_component"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(fcn_wroom_module)