cmake_minimum_required(VERSION 3.16)

# Make the repository root visible as a local ESP-IDF component. An application
# installed through Component Manager does not need EXTRA_COMPONENT_DIRS.
set(EXTRA_COMPONENT_DIRS "../..")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp_fir_basic)
