#pragma once

#include "esp_fir.h"

extern esp_fir_t fir_fase_r;
extern esp_fir_t fir_fase_s;
extern esp_fir_t fir_fase_t;

/* Prepare one immutable coefficient model and three independent histories. */
void app_fir_init(void);

/* Run the original and generic implementations over identical three-channel
 * workloads and report the best cycle count. Does nothing when disabled. */
void app_fir_benchmark(void);

/* Repeat the stored comparison without rerunning the benchmark. This is used
 * by the diagnostics reporter so the result remains visible after startup. */
void app_fir_report_benchmark(void);
