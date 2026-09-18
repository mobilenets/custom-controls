cmake_minimum_required(VERSION 3.22)

set(EXTRA_COMPONENT_DIRS
    "../common_components"
    "boards/waveshare_s3_8di_8ro"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(fcn_waveshare_s3_module)