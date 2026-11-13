cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# Build only main and its declared dependencies; exclude unused network stacks.
idf_build_set_property(MINIMAL_BUILD ON)
project(esp32-esp_mcp_4922)
