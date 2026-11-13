#pragma once
#include <assert.h>
#define ESP_ERROR_CHECK(expression) assert((expression) == 0)

void test_log(const char *, ...) __attribute__((format(printf, 1, 2)));
#define ESP_LOGI(tag, ...) test_log(__VA_ARGS__)
