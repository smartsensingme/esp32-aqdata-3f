#include "app_fir.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
int main(void) {
  app_fir_init();
  const float gain = app_fir_dc_gain();
  assert(fabsf(gain - 1.00951398f) < 1e-6f);
  const float offset = 1.58f, voltage = 1.68f;
  for (unsigned i = 0; i < 100; ++i) {
    const float output = esp_fir_process_symmetric(&fir_fase_r, voltage - offset);
    assert(fabsf(offset + output / gain - voltage) < 1e-6f);
  }
  puts("actual FIR DC reconstruction passed");
}
