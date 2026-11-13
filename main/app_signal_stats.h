#pragma once
#include <math.h>
#include <stdint.h>
#ifdef ESP_PLATFORM
#include "esp_attr.h"
#define APP_STATS_HOT IRAM_ATTR
#else
#define APP_STATS_HOT
#endif

/* Worker/startup statistics only. Welford avoids subtracting two nearly equal
 * DC powers when estimating small AC noise. RMS retains DC and has no sign. */
typedef struct {
  uint32_t n;
  float mean, m2;
} app_signal_stats_t;
static inline void app_signal_stats_add(app_signal_stats_t *s, float x) {
  const float d = x - s->mean;
  s->mean += d / (float)++s->n;
  s->m2 += d * (x - s->mean);
}
static inline float app_signal_ac_rms(const app_signal_stats_t *s) {
  return s->n ? sqrtf(fmaxf(0.0f, s->m2 / (float)s->n)) : 0.0f;
}
static inline float app_signal_rms(const app_signal_stats_t *s) {
  const float ac = app_signal_ac_rms(s);
  return s->n ? hypotf(s->mean, ac) : 0.0f;
}

static inline void APP_STATS_HOT app_compensated_add(float *sum,
                                                     float *correction,
                                                     float x) {
  const float y = x - *correction;
  const float t = *sum + y;
  *correction = (t - *sum) - y;
  *sum = t;
}

/* Worker-only single-frequency DFT, RMS of an integer-cycle sinusoid.
 * Samples in volts (or any consistent unit); mean is removed explicitly.
 * Caller chooses a frequency that spans an integer number of cycles in n/Fs.
 * No allocation; floating oscillator avoids per-sample transcendental math.
 * Invalid arguments return NaN. This is a spectral bin, not broadband RMS. */
static inline float app_signal_harmonic_rms(const float *samples, uint32_t n,
                                            float mean, float frequency_hz,
                                            float sample_rate_hz) {
  if (!samples || !n || !isfinite(mean) || !isfinite(frequency_hz) ||
      !isfinite(sample_rate_hz) || frequency_hz <= 0 ||
      frequency_hz >= 0.5f * sample_rate_hz)
    return NAN;
  const float angle = 6.283185307179586f * frequency_hz / sample_rate_hz;
  const float dc = cosf(angle), ds = sinf(angle);
  float c = 1, s = 0, real = 0, imag = 0;
  for (uint32_t i = 0; i < n; ++i) {
    const float value = samples[i] - mean;
    real += value * c;
    imag += value * s;
    const float next_c = c * dc - s * ds;
    s = s * dc + c * ds;
    c = next_c;
  }
  return 1.4142135623730951f * hypotf(real, imag) / (float)n;
}
