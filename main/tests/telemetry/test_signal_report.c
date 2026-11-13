#include "app_control.h"
#include <assert.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
static char log_text[8192];
void test_log(const char *format, ...) {
  size_t used = strlen(log_text);
  va_list args;
  va_start(args, format);
  vsnprintf(log_text + used, sizeof(log_text) - used, format, args);
  va_end(args);
}
int main(void) {
  const uint16_t codes[3] = {2048, 2048, 2048};
  const float filtered[3] = {1.68f, 1.68f, 1.68f};
  assert(app_control_request_capture());
  for (unsigned i = 0; i < RMS_BUFFER_SIZE; ++i) {
    const float adc[3] = {1.68f + (i % 2 ? 0.05f : -0.05f), 1.68f, 1.68f};
    assert(app_control_store_sample(codes, adc, filtered) ==
           (i + 1 == RMS_BUFFER_SIZE));
  }
  float current;
  assert(app_control_calc_rms_report(&current, NULL, NULL, true) &&
         current == 0);
  float mean, rms, noise;
  const char *raw = strstr(log_text, "ADC R:");
  assert(raw && sscanf(raw, "ADC R: media=%f RMS=%f V; desvio_padrao=%f", &mean,
                       &rms, &noise) == 3);
  assert(fabsf(mean - 1.68f) < 1e-5f);
  assert(fabsf(rms - hypotf(1.68f, 0.05f)) < 1e-5f);
  assert(fabsf(noise - 50) < 0.01f);
  const char *fir = strstr(log_text, "FIR R (DC normalizado):");
  assert(fir &&
         sscanf(fir,
                "FIR R (DC normalizado): media=%f RMS=%f V; desvio_padrao=%f",
                &mean, &rms, &noise) == 3);
  assert(fabsf(mean - 1.68f) < 1e-5f && fabsf(rms - 1.68f) < 1e-5f &&
         noise == 0);
  assert(!strstr(log_text, "ADC S:"));
  assert(strstr(log_text, "Compensacao estimada S:") &&
         strstr(log_text, "Compensacao estimada T:"));
  const char *comp = strstr(log_text, "Compensacao estimada R:");
  assert(comp &&
         sscanf(comp, "Compensacao estimada R: Icomp_mean=%f Icomp_rms=%f A",
                &mean, &rms) == 2);
  assert(mean == 0 && rms == 0);
  const char *dac = strstr(log_text, "DAC R (estimado):");
  assert(dac &&
         sscanf(dac, "DAC R (estimado): media=%f RMS=%f V", &mean, &rms) == 2);
  assert(fabsf(mean - DAC_REFERENCE_V * 0.5f) < 1e-5f);
  log_text[0] = '\0';
  const uint16_t active[3] = {2592, 2048, 2048};
  assert(app_control_request_capture());
  for (unsigned i = 0; i < RMS_BUFFER_SIZE; ++i)
    (void)app_control_store_sample(active, filtered, filtered);
  assert(app_control_calc_rms(&current, NULL, NULL));
  assert(!strstr(log_text, "ADC R:") && !strstr(log_text, "DAC R"));
  comp = strstr(log_text, "Compensacao estimada R:");
  assert(comp &&
         sscanf(comp, "Compensacao estimada R: Icomp_mean=%f Icomp_rms=%f A",
                &mean, &rms) == 2);
  assert(fabsf(mean - app_control_dac_to_primary_current(2592)) < 1e-5f);
  assert(fabsf(mean - rms) < 1e-5f);
  puts("ADC/FIR/current/DAC capture and reporting passed");
}
