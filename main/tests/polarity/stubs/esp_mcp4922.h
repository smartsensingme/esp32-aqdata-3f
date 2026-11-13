#pragma once
#include "fakes.h"
typedef struct { int dummy; } mcp4922_context_t;
esp_err_t mcp4922_write_channels(mcp4922_context_t *, const uint16_t *);
