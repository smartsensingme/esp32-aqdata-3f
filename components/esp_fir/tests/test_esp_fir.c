#include "esp_fir.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static float next_sample(void) {
  static unsigned state = 0x12345678U;
  state = state * 1664525U + 1013904223U;
  return (float)((int)(state >> 8) - 0x7fffff) / 8388608.0f;
}

static float reference(const float *h, size_t taps, float *history, float x) {
  memmove(history + 1, history, (taps - 1U) * sizeof(*history));
  history[0] = x;
  float y = 0.0f;
  for (size_t i = 0; i < taps; ++i)
    y += h[i] * history[i];
  return y;
}

static void compare(const float *full, const float *stored, size_t stored_count,
                    size_t taps, esp_fir_structure_t structure,
                    esp_fir_coefficient_layout_t layout) {
  esp_fir_model_t model;
  esp_fir_config_t config = {
      .coefficients = stored,
      .coefficient_count = stored_count,
      .tap_count = taps,
      .structure = structure,
      .coefficient_layout = layout,
      .startup = ESP_FIR_STARTUP_ZERO,
      .symmetry_tolerance = 1e-6f,
  };
  assert(esp_fir_prepare(&model, &config) == ESP_OK);
  float *history = calloc(esp_fir_history_size(taps), sizeof(*history));
  float *reference_history = calloc(taps, sizeof(*reference_history));
  assert(history && reference_history);
  esp_fir_t filter;
  assert(esp_fir_init(&filter, &model, history, esp_fir_history_size(taps)) ==
         ESP_OK);
  for (size_t i = 0; i < 10000; ++i) {
    const float x = i == 0 ? 1.0f : next_sample();
    const float actual = esp_fir_process(&filter, x);
    const float expected = reference(full, taps, reference_history, x);
    assert(fabsf(actual - expected) <= 2e-5f * (1.0f + fabsf(expected)));
  }
  free(reference_history);
  free(history);
}

static void structures(void) {
  const float general[] = {.2f, -.1f, .4f, .3f, -.2f, .05f};
  compare(general, general, 6, 6, ESP_FIR_GENERAL, ESP_FIR_COEFFICIENTS_FULL);

  const float symmetric_odd[] = {.1f, -.2f, .3f, .7f, .3f, -.2f, .1f};
  const float symmetric_odd_folded[] = {.1f, -.2f, .3f, .7f};
  compare(symmetric_odd, symmetric_odd, 7, 7, ESP_FIR_SYMMETRIC,
          ESP_FIR_COEFFICIENTS_FULL);
  compare(symmetric_odd, symmetric_odd_folded, 4, 7, ESP_FIR_SYMMETRIC,
          ESP_FIR_COEFFICIENTS_FOLDED);

  const float symmetric_even[] = {.1f, -.3f, .5f, .5f, -.3f, .1f};
  compare(symmetric_even, symmetric_even, 6, 6, ESP_FIR_SYMMETRIC,
          ESP_FIR_COEFFICIENTS_FULL);
  compare(symmetric_even, symmetric_even, 3, 6, ESP_FIR_SYMMETRIC,
          ESP_FIR_COEFFICIENTS_FOLDED);

  const float antisymmetric_odd[] = {.1f, -.3f, .5f, 0, -.5f, .3f, -.1f};
  compare(antisymmetric_odd, antisymmetric_odd, 7, 7, ESP_FIR_ANTISYMMETRIC,
          ESP_FIR_COEFFICIENTS_FULL);
  compare(antisymmetric_odd, antisymmetric_odd, 3, 7, ESP_FIR_ANTISYMMETRIC,
          ESP_FIR_COEFFICIENTS_FOLDED);

  const float antisymmetric_even[] = {.1f, -.3f, .5f, -.5f, .3f, -.1f};
  compare(antisymmetric_even, antisymmetric_even, 6, 6, ESP_FIR_ANTISYMMETRIC,
          ESP_FIR_COEFFICIENTS_FULL);
  compare(antisymmetric_even, antisymmetric_even, 3, 6, ESP_FIR_ANTISYMMETRIC,
          ESP_FIR_COEFFICIENTS_FOLDED);
  puts("PASS general, odd/even symmetric and antisymmetric kernels");
}

static void high_order(void) {
  enum { TAPS = 511, STORED = 256 };
  float *full = calloc(TAPS, sizeof(*full));
  float *folded = calloc(STORED, sizeof(*folded));
  assert(full && folded);
  for (size_t i = 0; i < STORED; ++i) {
    folded[i] = (float)sin((double)(i + 1U)) / (float)TAPS;
    full[i] = folded[i];
    full[TAPS - 1U - i] = folded[i];
  }
  compare(full, folded, STORED, TAPS, ESP_FIR_SYMMETRIC,
          ESP_FIR_COEFFICIENTS_FOLDED);
  free(folded);
  free(full);
  puts("PASS 511-tap filter without a compiled-in order limit");
}

static void startup_and_validation(void) {
  const float h[] = {.25f, .5f, .25f};
  esp_fir_config_t config = {h,
                             3,
                             3,
                             ESP_FIR_SYMMETRIC,
                             ESP_FIR_COEFFICIENTS_FULL,
                             ESP_FIR_STARTUP_PRIME_FIRST_SAMPLE,
                             0};
  esp_fir_model_t model = {0};
  assert(esp_fir_prepare(&model, &config) == ESP_OK);
  float history[6];
  esp_fir_t a;
  assert(esp_fir_init(&a, &model, history, 6) == ESP_OK);
  assert(esp_fir_process_symmetric(&a, 2.0f) == 2.0f);
  assert(esp_fir_process_symmetric(&a, 2.0f) == 2.0f);
  assert(esp_fir_reset(&a) == ESP_OK);
  assert(esp_fir_process_symmetric(&a, -3.0f) == -3.0f);

  esp_fir_t b;
  float other_history[6];
  assert(esp_fir_init(&b, &model, other_history, 6) == ESP_OK);
  assert(esp_fir_process_symmetric(&b, 1.0f) == 1.0f);
  assert(a.history != b.history);

  esp_fir_model_t saved = model;
  config.coefficient_count = 2;
  assert(esp_fir_prepare(&model, &config) != ESP_OK);
  assert(memcmp(&model, &saved, sizeof(model)) == 0);
  config.coefficient_count = 3;
  config.structure = ESP_FIR_ANTISYMMETRIC;
  assert(esp_fir_prepare(&model, &config) != ESP_OK);
  config.structure = ESP_FIR_SYMMETRIC;
  config.symmetry_tolerance = -1;
  assert(esp_fir_prepare(&model, &config) != ESP_OK);
  config.symmetry_tolerance = 0;
  config.structure = ESP_FIR_GENERAL;
  config.coefficient_layout = ESP_FIR_COEFFICIENTS_FOLDED;
  assert(esp_fir_prepare(&model, &config) != ESP_OK);
  config.coefficient_layout = (esp_fir_coefficient_layout_t)-1;
  assert(esp_fir_prepare(&model, &config) != ESP_OK);
  config.coefficient_layout = ESP_FIR_COEFFICIENTS_FULL;
  config.startup = (esp_fir_startup_t)-1;
  assert(esp_fir_prepare(&model, &config) != ESP_OK);
  assert(esp_fir_init(&a, &saved, history, 5) == ESP_ERR_INVALID_SIZE);
  assert(esp_fir_history_size(0) == 0);
  assert(esp_fir_history_size((size_t)-1) == 0);
  puts("PASS startup priming, reset, independent states and validation");
}

int main(void) {
  structures();
  high_order();
  startup_and_validation();
  puts("All esp_fir host tests passed.");
  return 0;
}
