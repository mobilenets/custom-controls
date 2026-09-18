cmake_minimum_required(VERSION 3.22)

set(EXTRA_COMPONENT_DIRS
    "../common_components"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(fcn_wroom_module)