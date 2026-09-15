#include "esp_fir.h"
#include "fir_pmr.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

static const float coefficients[] = {
    -2.649769434988316e-02f, -1.973714929907618e-02f, -5.132740945614728e-03f,
    3.129857530427908e-02f,  8.558767456757677e-02f,  1.446596688184109e-01f,
    1.906123538202989e-01f,  2.079326067165584e-01f,
};

static float sample(size_t i) {
  return .7f * sinf((float)i * .031f) + .2f * cosf((float)i * .173f) +
         (float)((int)(i % 17U) - 8) * .003f;
}

int main(void) {
  esp_fir_config_t config = {
      .coefficients = coefficients,
      .coefficient_count = sizeof(coefficients) / sizeof(coefficients[0]),
      .tap_count = FIR_PMR_TAPS,
      .structure = ESP_FIR_SYMMETRIC,
      .coefficient_layout = ESP_FIR_COEFFICIENTS_FOLDED,
      .startup = ESP_FIR_STARTUP_PRIME_FIRST_SAMPLE,
      .symmetry_tolerance = 0,
  };
  esp_fir_model_t model;
  assert(esp_fir_prepare(&model, &config) == ESP_OK);
  float history[2 * FIR_PMR_TAPS];
  esp_fir_t generic;
  assert(esp_fir_init(&generic, &model, history,
                      sizeof(history) / sizeof(history[0])) == ESP_OK);
  fir_pmr_state_t specific;
  fir_pmr_init(&specific);

  float max_error = 0;
  for (size_t i = 0; i < 100000; ++i) {
    const float x = sample(i);
    const float old_y = fir_pmr_process(&specific, x);
    const float new_y = esp_fir_process_symmetric(&generic, x);
    const float error = fabsf(old_y - new_y);
    if (error > max_error)
      max_error = error;
    assert(error <= 2e-7f);
  }
  printf("PASS fir_pmr equivalence over 100000 samples; max abs error %.9g\n",
         max_error);
  return 0;
}
