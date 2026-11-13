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

static inline void APP_STATS_HOT app_compensated_add(float *sum, float *correction, float x) {
  const float y = x - *correction;
  const float t = *sum + y;
  *correction = (t - *sum) - y;
  *sum = t;
}
