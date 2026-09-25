cmake_minimum_required(VERSION 3.16)

# Use the component at this repository's root. A consuming project installed
# through the Component Manager does not need EXTRA_COMPONENT_DIRS.
set(EXTRA_COMPONENT_DIRS "../..")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp_pr_basic)
