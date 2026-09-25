#pragma once

#include "esp_fir.h"

extern esp_fir_t fir_fase_r;
extern esp_fir_t fir_fase_s;
extern esp_fir_t fir_fase_t;

/* Prepare one immutable coefficient model and three independent histories. */
void app_fir_init(void);
