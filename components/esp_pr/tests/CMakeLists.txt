cmake_minimum_required(VERSION 3.16)
project(esp_pr_host_tests C)
enable_testing()
add_executable(test_esp_pr ../esp_pr.c test_esp_pr.c)
target_include_directories(test_esp_pr PRIVATE ../include)
target_compile_features(test_esp_pr PRIVATE c_std_11)
target_compile_options(test_esp_pr PRIVATE -Wall -Wextra -Werror -pedantic -UNDEBUG)
target_link_libraries(test_esp_pr PRIVATE m)
add_test(NAME esp_pr COMMAND test_esp_pr)

# Compile the real application adapter and voltage conversion against minimal
# platform declarations; no peripheral or RTOS behavior is simulated here.
add_executable(test_app_integration ../esp_pr.c test_app_integration.c
    ../../../main/app_control.c)
target_include_directories(test_app_integration PRIVATE
    ../include ../../../main stubs)
target_compile_features(test_app_integration PRIVATE c_std_11)
target_compile_options(test_app_integration PRIVATE -Wall -Wextra -Werror -pedantic -UNDEBUG)
target_link_libraries(test_app_integration PRIVATE m)
add_test(NAME app_pr_integration COMMAND test_app_integration)
