#include "app_control.h"
#include "app_tuning.h"
#include <assert.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static char logs[12000];
void test_log(const char *format, ...) {
  size_t length = strlen(logs);
  va_list args;
  va_start(args, format);
  vsnprintf(logs + length, sizeof(logs) - length, format, args);
  va_end(args);
}
static void init(void) {
  logs[0] = 0;
  app_tuning_init(false);
}
int main(void) {
  // Real portable PRs: complete all stages, respect output envelope, finite
  // run.
  init();
  unsigned frames = 0;
  while (!app_tuning_done()) {
    float e = 0.001f * sinf(frames * 0.0261799388f);
    float output = app_tuning_step(e, e, 1);
    assert(isfinite(output) && fabsf(output) <= 128.0f * 12 / 4095);
    assert(++frames <= 9 * 8 * 14400);
  }
  assert(frames == 9 * 8 * 14400);
  app_tuning_report();
  assert(strstr(logs, "completed all stages"));
  assert(strstr(logs, "stage=8") && strstr(logs, "n=86400"));
  assert(app_tuning_step(0, 0, 1) == 0);

  // Validation preserves the selected controller state for 60 continuous
  // seconds.
  logs[0] = 0;
  app_tuning_init(true);
  frames = 0;
  while (!app_tuning_done()) {
    assert(app_tuning_step(0, 0, 1) == 0);
    assert(++frames <= 68 * 14400);
  }
  assert(frames == 68 * 14400);
  app_tuning_report();
  assert(strstr(logs, "n=835200"));

  // Finite DC profile uses the same PR+I as normal control, at its own limit.
  logs[0] = 0;
  app_tuning_init_dc(1.55f, 0.9f);
  // DC convergence needs an enabled integrator even when AC gain selection is 0.
  app_control_init_with_limit(6.0f);
  assert(app_parallel_init(&pr_fase_r, pr_fase_r.pr.model, 2.0f, 6.0f) == ESP_OK);
  frames = 0;
  float current = 0.0f, command = 0.0f;
  while (!app_tuning_done()) {
    current += (app_control_dac_to_primary_current(
                    app_control_voltage_to_dac(command)) -
                current) /
               72.0f;
    float error = 0.3f * (0.9f - current);
    command = app_tuning_step(error, error, 1);
    assert(isfinite(command) && fabsf(command) <= 6.0f);
    assert(++frames <= 64 * 14400);
  }
  assert(frames == 64 * 14400 && command == 0.0f);
  app_tuning_report();
  const char *dc = strstr(logs, "DC stage=1");
  assert(dc && strstr(dc, "residual check=yes"));
  float estimated = 0;
  assert(sscanf(strstr(dc, "Icomp_mean="), "Icomp_mean=%f", &estimated) == 1);
  assert(fabsf(estimated - 0.9f) < 0.01f);
  float estimated_rms = 0;
  assert(sscanf(strstr(dc, "Icomp_rms="), "Icomp_rms=%f", &estimated_rms) == 1);
  assert(estimated_rms + 1e-6f >= fabsf(estimated));

  // Zero-reference reporting avoids division by zero and percentage claims.
  logs[0] = 0;
  app_tuning_init_dc(1.55f, 0.0f);
  for (unsigned i = 0; i < 64 * 14400; ++i)
    assert(app_tuning_step(0, 0, 1) == 0);
  assert(app_tuning_done());
  app_tuning_report();
  assert(strstr(logs, "ZERO CURRENT CHECK"));
  assert(!strstr(logs, "Reference comparison"));

  // Neutral measurement discards the controller even if its integral saturates.
  logs[0] = 0;
  app_tuning_init_neutral(1.55f, 0.0f);
  for (unsigned i = 0; i < 84 * 14400; ++i)
    assert(app_tuning_step(0.1f, 0.1f, 1) == 0);
  assert(app_tuning_done());
  app_tuning_report();
  assert(strstr(logs, "NEUTRAL ONLY") && strstr(logs, "completed all stages"));
  float measured_offset;
  assert(app_tuning_zero_offset(1.0f, &measured_offset));
  assert(fabsf(measured_offset - 1.65f) < 0.0001f);

  // A drifting zero must not be eligible for persistence.
  app_tuning_init_neutral(1.55f, 0.0f);
  for (unsigned i = 0; i < 84 * 14400; ++i) {
    const float e = i >= 74 * 14400 ? 0.01f : 0.0f;
    assert(app_tuning_step(e, e, 1) == 0);
  }
  assert(!app_tuning_zero_offset(1.0f, &measured_offset));

  // Hard sensor/backlog/nonfinite stops return zero immediately.
  init();
  assert(app_tuning_step(0.31f, 0, 1) == 0 && app_tuning_done());
  init();
  assert(app_tuning_step(0, 0, 5) == 0 && app_tuning_done());
  init();
  assert(app_tuning_step(0, NAN, 1) == 0 && app_tuning_done());
  init();
  app_tuning_abort();
  assert(app_tuning_done());

  // Short rail excursions are allowed; persistent AC-style clipping is timed
  // by 1 s duty windows, even though no single excursion lasts 100 ms.
  logs[0] = 0;
  app_tuning_init_ac(1.55f, 0.549f);
  for (unsigned i = 0; i < 4 * 14400; ++i)
    app_tuning_step(0, 0, 1);
  for (unsigned i = 0; i < 14400 / 2; ++i)
    app_tuning_step(0, (i % 240 < 48) ? 100.0f : 0.0f, 1);
  assert(!app_tuning_done());
  for (unsigned i = 0; i < 2 * 14400; ++i)
    app_tuning_step(0, 0, 1);
  assert(!app_tuning_done());
  // Periodic rail clipping sustained across five full windows does abort.
  app_tuning_init_ac(1.55f, 0.549f);
  for (unsigned i = 0; i < 4 * 14400; ++i)
    app_tuning_step(0, 0, 1);
  unsigned saturated_frames = 0;
  while (!app_tuning_done() && saturated_frames < 6 * 14400) {
    app_tuning_step(0, (saturated_frames % 240 < 48) ? 100.0f : 0.0f, 1);
    ++saturated_frames;
  }
  assert(app_tuning_done() && saturated_frames == 5 * 14400);
  app_tuning_report();
  assert(strstr(logs, "sustained DAC rail saturation"));

  // AC compares total current RMS, not the near-zero signed mean.
  logs[0] = 0;
  app_tuning_init_ac(1.55f, 0.560f);
  assert(pr_fase_r.pr.model->output_max == 6.0f);
  assert(pr_fase_r.integral_enabled);
  assert(pr_fase_r.integral.config.output_max == 6.0f);
  for (unsigned i = 0; i < 64 * 14400; ++i) {
    const float e = 0.001f * sinf((float)(i % 240) * (2.0f * 3.14159265f / 240));
    app_tuning_step(e, e, 1);
  }
  assert(app_tuning_done());
  app_tuning_report();
  const char *ac_line = strstr(logs, "AC stage=1");
  float ac_mean, ac_rms;
  assert(ac_line && sscanf(ac_line, "AC stage=1 n=144000 Icomp_mean=%f Icomp_rms=%f", &ac_mean, &ac_rms) == 2);
  assert(ac_rms > fabsf(ac_mean) + 0.0001f);
  const char *comparison = strstr(logs, "Reference comparison (external current");
  float reference, percent;
  assert(comparison && sscanf(comparison, "Reference comparison (external current %f A): %f", &reference, &percent) == 2);
  assert(fabsf(percent - 100.0f * (ac_rms / 0.560f - 1.0f)) < 0.02f);
  assert(strstr(logs, "fundamental60_RMS"));
  // Separate an even harmonic from the nominal 60 Hz channel.
  logs[0] = 0;
  app_tuning_init_ac(1.55f, 0.549f);
  for (unsigned i = 0; i < 64 * 14400; ++i) {
    const float e = 0.01f * sinf((float)(i % 120) * (2.0f * 3.14159265f / 120));
    app_tuning_step(e, e, 1);
  }
  assert(app_tuning_done());
  app_tuning_report();
  const char *even = strstr(logs, "AC residual nominal even harmonics:");
  assert(even);
  float h120, h240, h360;
  assert(sscanf(even, "AC residual nominal even harmonics: 120=%f 240=%f 360=%f", &h120, &h240, &h360) == 3);
  assert(fabsf(h120 - 7.071f) < 0.1f && h240 < 0.1f && h360 < 0.1f);
  puts("bounded tuning tests passed");
}
