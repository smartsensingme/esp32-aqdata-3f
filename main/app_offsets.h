#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>
#define APP_OFFSET_SAMPLE_COUNT 1440U
#define APP_OFFSET_DEFAULT_V 1.65f

/* Population statistics in volts, computed after stopping DMA. */
typedef struct {
  float mean, rms, standard_deviation, minimum, maximum;
} app_offset_signal_report_t;
/* NVS calibration_v2 blob: native little-endian uint32/IEEE float, version=2,
 * sample count, offsets R/S/T, then raw and DC-normalized FIR reports R/S/T.
 * All fields are persisted in ONE blob. Older records are ignored;
 * missing/invalid v2 uses defaults.
 */
typedef struct {
  uint32_t version, sample_count;
  float offsets[3];
  app_offset_signal_report_t raw[3], filtered[3];
} app_offset_record_t;

/* Boot-only: load v2/default and debounce active-low calibration GPIO.
 * No acquisition or NVS writes. DAC must already be neutral. */
esp_err_t app_offsets_init(const float *adc_voltage_lut, float offsets[3]);
bool app_offsets_calibration_requested(void);
/* All three phases, zero primary current, ADC stopped. Validates samples,
 * computes/logs full-population statistics, sorts FIR rows in place and uses
 * a 10%-per-tail trimmed mean as each offset. Writes one versioned NVS blob;
 * RAM offsets change only after successful commit. Raw rows are read-only.
 * No call from a periodic loop; caller owns buffers through return.
 */
esp_err_t
app_offsets_commit_calibration(const float raw[3][APP_OFFSET_SAMPLE_COUNT],
                               float filtered[3][APP_OFFSET_SAMPLE_COUNT],
                               float offsets[3]);
