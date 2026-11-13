#pragma once
#include "esp_attr.h"
#include <stdbool.h>
#define APP_DC_ACTIVE_SECONDS 60U
#define APP_TUNING_SATURATION_SECONDS 5U
// Each 1 s window must have >=10% samples at DAC rails to count as sustained.

/* Temporary finite R-only commissioning run at the normal frame rate.
 * Models/storage prepared before DMA, no flash writes or logs in step(). */
// validate=true: 8 s neutral + 60 s selected gains; false: nine-stage sweep.
void app_tuning_init(bool validate);
// DC commissioning: 4 s baseline + 60 s PR+I; 6 Vsoft total bound (full DAC scale),
// sample the final 10 s, no offset recalibration. adc_offset is loaded from
// NVS.
void app_tuning_init_dc(float adc_offset, float reference_current);
// AC commissioning: same duration/bounds, full common DAC range and coordinated tracking, reference is total RMS.
void app_tuning_init_ac(float adc_offset, float reference_rms);
// Same finite acquisition/workload but no controller output reaches the DAC.
void app_tuning_init_neutral(float adc_offset, float reference_current);
bool app_tuning_done(void);
float app_tuning_step(float raw_error, float filtered_error, unsigned backlog);
void app_tuning_abort(void);
/* Call only after DMA stops and all DACs have been commanded neutral. */
void app_tuning_report(void);

// Valid only after a completed neutral run: mean FIR error of final four
// windows, normalized back to ADC volts. No persistence/actuation here.
bool app_tuning_zero_offset(float inverse_fir_gain, float *offset);
