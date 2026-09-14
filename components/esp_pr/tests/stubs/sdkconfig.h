#pragma once
#include "esp_attr.h"
#include <assert.h>
#define ESP_ERROR_CHECK(expression) assert((expression) == ESP_OK)
#define CONFIG_APP_PR_TRACKING_TIME_US 10000
