#pragma once

#include "esp_err.h"
#include <stdbool.h>
#define APP_OFFSET_SAMPLE_COUNT 1440U

#define APP_OFFSET_DEFAULT_V 1.65f

/* Startup: load/default offsets and debounce the active-low request. No ADC
 * acquisition or NVS commit occurs here. Control owner later measures via DMA.
 */
esp_err_t app_offsets_init(const float *adc_voltage_lut, float offsets[3]);

/* Load/default only: commissioning must never recalibrate or overwrite NVS. */
esp_err_t app_offsets_load(const float *adc_voltage_lut, float offsets[3]);

/* Request is sampled at boot, but acquisition/commit is owned by Core 1.
 * Commit only after ADC DMA is stopped. Inactive phases retain their offsets.
 * Input rows contain calibrated ADC voltages; caller owns their lifetime.
 * Rows selected by mask are sorted in place (10% trimmed mean). */
bool app_offsets_calibration_requested(void);
esp_err_t app_offsets_commit_samples(float samples[3][APP_OFFSET_SAMPLE_COUNT],
                                     unsigned mask, float offsets[3]);

// Startup/commissioning only, ADC stopped. Commit measured means atomically.
esp_err_t app_offsets_commit_values(const float values[3], unsigned mask,
                                    float offsets[3]);
