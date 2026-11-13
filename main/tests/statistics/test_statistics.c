#include "app_signal_stats.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
  app_signal_stats_t positive = {0}, negative = {0}, dc = {0};
  for (unsigned i = 0; i < 1440; ++i) {
    const float noise = i % 2 ? 0.05f : -0.05f;
    app_signal_stats_add(&positive, 1.68f + noise);
    app_signal_stats_add(&negative, -1.68f + noise);
    app_signal_stats_add(&dc, 1.68f);
  }
  assert(fabsf(positive.mean - 1.68f) < 1e-5f);
  assert(fabsf(app_signal_ac_rms(&positive) - 0.05f) < 1e-5f);
  assert(fabsf(app_signal_rms(&positive) - hypotf(1.68f, 0.05f)) < 1e-5f);
  assert(app_signal_rms(&positive) > positive.mean);
  assert(negative.mean < 0 && app_signal_rms(&negative) > 0);
  assert(fabsf(app_signal_rms(&dc) - 1.68f) < 1e-6f);
  assert(app_signal_ac_rms(&dc) == 0);
  float waveform[1440];
  for (unsigned i = 0; i < 1440; ++i) {
    const float t = (float)i / 14400;
    waveform[i] = 1.65f + 0.1f * sinf(6.283185307179586f * 60 * t) +
                  0.05f * cosf(6.283185307179586f * 180 * t);
  }
  assert(fabsf(app_signal_harmonic_rms(waveform, 1440, 1.65f, 60, 14400) -
               0.1f / sqrtf(2)) < 1e-5f);
  assert(fabsf(app_signal_harmonic_rms(waveform, 1440, 1.65f, 180, 14400) -
               0.05f / sqrtf(2)) < 1e-5f);
  assert(app_signal_harmonic_rms(waveform, 1440, 1.65f, 120, 14400) < 1e-5f);
  assert(isnan(app_signal_harmonic_rms(waveform, 0, 1.65f, 60, 14400)));
  assert(isnan(app_signal_harmonic_rms(waveform, 1440, 1.65f, 7200, 14400)));
  puts("mean/DC+AC RMS and separated harmonic tests passed");
}
