#pragma once

#include "esp_mcp4922.h"

/* Startup-only open-loop R test. LUT: 4096 calibrated ADC1 voltages, 12 dB.
 * Never changes controller polarity or NVS. Reports five consistent responses
 * or an inconclusive/error result. Restores all DACs to midpoint unless the
 * DAC driver fails (latched contexts must not be retried). No DMA may be
 * active.
 */
void app_polarity_run(mcp4922_context_t *dac, const float *lut, unsigned step);
