#include "esp_fir.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
  COMPARISON_SAMPLE_COUNT = 10000,
  HIGH_ORDER_TAP_COUNT = 511,
  HIGH_ORDER_STORED_COUNT = 256,
};

/**
 * @brief Generate a deterministic sample in approximately [-1, 1).
 *
 * A fixed linear-congruential sequence makes every test run repeatable while
 * exercising more histories than a short hand-written input vector.
 */
static float next_sample(void) {
  static unsigned state = 0x12345678U;

  state = state * 1664525U + 1013904223U;
  return (float)((int)(state >> 8) - 0x7fffff) / 8388608.0f;
}

/**
 * @brief Evaluate one sample using independent direct convolution.
 *
 * This intentionally shifts a conventional N-sample history. It does not share
 * the component's mirrored circular-buffer algorithm, so agreement can expose
 * indexing, wraparound, and coefficient-layout errors in the optimized kernels.
 *
 * @param full_coefficients Complete impulse response with @p tap_count entries.
 * @param tap_count Number of coefficients and reference-history elements.
 * @param history Mutable conventional history, initially zero.
 * @param sample Current input sample.
 * @return Direct-convolution output for the current sample.
 */
static float direct_convolution(const float *full_coefficients,
                                size_t tap_count, float *history,
                                float sample) {
  memmove(history + 1, history,
          (tap_count - 1U) * sizeof(*history));
  history[0] = sample;

  float output = 0.0f;

  for (size_t index = 0; index < tap_count; ++index)
    output += full_coefficients[index] * history[index];

  return output;
}

/**
 * @brief Compare one component configuration with direct convolution.
 *
 * The caller supplies both the complete response used by the independent
 * reference and the full or folded array used by esp_fir. The impulse at the
 * first sample exposes startup and coefficient-order errors; deterministic
 * pseudo-random samples then exercise circular-history wraparound repeatedly.
 *
 * Heap allocation is acceptable here because this is a host test, not the
 * component's real-time processing path.
 */
static void compare_with_reference(
    const float *full_coefficients, const float *stored_coefficients,
    size_t stored_count, size_t tap_count, esp_fir_structure_t structure,
    esp_fir_coefficient_layout_t layout) {
  const esp_fir_config_t config = {
      .coefficients = stored_coefficients,
      .coefficient_count = stored_count,
      .tap_count = tap_count,
      .structure = structure,
      .coefficient_layout = layout,
      .startup = ESP_FIR_STARTUP_ZERO,
      .symmetry_tolerance = 1e-6f,
  };

  esp_fir_model_t model;
  assert(esp_fir_prepare(&model, &config) == ESP_OK);

  const size_t history_count = esp_fir_history_size(tap_count);
  float *history = calloc(history_count, sizeof(*history));
  float *reference_history = calloc(tap_count, sizeof(*reference_history));

  assert(history != NULL);
  assert(reference_history != NULL);

  esp_fir_t filter;
  assert(esp_fir_init(&filter, &model, history, history_count) == ESP_OK);

  for (size_t index = 0; index < COMPARISON_SAMPLE_COUNT; ++index) {
    const float sample = index == 0 ? 1.0f : next_sample();
    const float actual = esp_fir_process(&filter, sample);
    const float expected = direct_convolution(
        full_coefficients, tap_count, reference_history, sample);
    const float tolerance = 2e-5f * (1.0f + fabsf(expected));

    assert(fabsf(actual - expected) <= tolerance);
  }

  free(reference_history);
  free(history);
}

/**
 * @brief Cover every structure, coefficient layout, and tap-count parity.
 *
 * General filters support only full storage. Symmetric and antisymmetric
 * filters are each checked with odd/even lengths and full/folded storage.
 */
static void test_structures_and_layouts(void) {
  const float general[] = {.2f, -.1f, .4f, .3f, -.2f, .05f};

  compare_with_reference(general, general, 6, 6, ESP_FIR_GENERAL,
                         ESP_FIR_COEFFICIENTS_FULL);

  const float symmetric_odd[] = {.1f, -.2f, .3f, .7f, .3f, -.2f, .1f};
  const float symmetric_odd_folded[] = {.1f, -.2f, .3f, .7f};

  compare_with_reference(symmetric_odd, symmetric_odd, 7, 7,
                         ESP_FIR_SYMMETRIC, ESP_FIR_COEFFICIENTS_FULL);
  compare_with_reference(symmetric_odd, symmetric_odd_folded, 4, 7,
                         ESP_FIR_SYMMETRIC, ESP_FIR_COEFFICIENTS_FOLDED);

  const float symmetric_even[] = {.1f, -.3f, .5f, .5f, -.3f, .1f};

  compare_with_reference(symmetric_even, symmetric_even, 6, 6,
                         ESP_FIR_SYMMETRIC, ESP_FIR_COEFFICIENTS_FULL);
  compare_with_reference(symmetric_even, symmetric_even, 3, 6,
                         ESP_FIR_SYMMETRIC, ESP_FIR_COEFFICIENTS_FOLDED);

  const float antisymmetric_odd[] = {
      .1f, -.3f, .5f, 0, -.5f, .3f, -.1f};

  compare_with_reference(antisymmetric_odd, antisymmetric_odd, 7, 7,
                         ESP_FIR_ANTISYMMETRIC,
                         ESP_FIR_COEFFICIENTS_FULL);
  compare_with_reference(antisymmetric_odd, antisymmetric_odd, 3, 7,
                         ESP_FIR_ANTISYMMETRIC,
                         ESP_FIR_COEFFICIENTS_FOLDED);

  const float antisymmetric_even[] = {.1f, -.3f, .5f, -.5f, .3f, -.1f};

  compare_with_reference(antisymmetric_even, antisymmetric_even, 6, 6,
                         ESP_FIR_ANTISYMMETRIC,
                         ESP_FIR_COEFFICIENTS_FULL);
  compare_with_reference(antisymmetric_even, antisymmetric_even, 3, 6,
                         ESP_FIR_ANTISYMMETRIC,
                         ESP_FIR_COEFFICIENTS_FOLDED);

  puts("PASS general, odd/even symmetric and antisymmetric kernels");
}

/**
 * @brief Verify that filter order is limited by caller storage, not the API.
 *
 * A 511-tap symmetric response is generated in folded form, expanded only for
 * the independent reference, and processed through the same 10,000-sample
 * comparison used for the small filters.
 */
static void test_high_order_filter(void) {
  float *full = calloc(HIGH_ORDER_TAP_COUNT, sizeof(*full));
  float *folded = calloc(HIGH_ORDER_STORED_COUNT, sizeof(*folded));

  assert(full != NULL);
  assert(folded != NULL);

  for (size_t index = 0; index < HIGH_ORDER_STORED_COUNT; ++index) {
    folded[index] =
        (float)sin((double)(index + 1U)) / (float)HIGH_ORDER_TAP_COUNT;
    full[index] = folded[index];
    full[HIGH_ORDER_TAP_COUNT - 1U - index] = folded[index];
  }

  compare_with_reference(full, folded, HIGH_ORDER_STORED_COUNT,
                         HIGH_ORDER_TAP_COUNT, ESP_FIR_SYMMETRIC,
                         ESP_FIR_COEFFICIENTS_FOLDED);

  free(folded);
  free(full);

  puts("PASS 511-tap filter without a compiled-in order limit");
}

/**
 * @brief Verify primed startup, reset, independent state, and invalid inputs.
 *
 * The valid three-tap filter has unity DC gain, so first-sample priming must
 * return a constant input unchanged. Later scenarios confirm reset and stream
 * independence before deliberately mutating one configuration field at a time.
 */
static void test_startup_lifecycle_and_validation(void) {
  const float coefficients[] = {.25f, .5f, .25f};
  esp_fir_config_t config = {
      .coefficients = coefficients,
      .coefficient_count = 3,
      .tap_count = 3,
      .structure = ESP_FIR_SYMMETRIC,
      .coefficient_layout = ESP_FIR_COEFFICIENTS_FULL,
      .startup = ESP_FIR_STARTUP_PRIME_FIRST_SAMPLE,
      .symmetry_tolerance = 0,
  };

  esp_fir_model_t model = {0};
  assert(esp_fir_prepare(&model, &config) == ESP_OK);

  /* Priming and reset must reproduce the response of a prior constant input. */
  float history_a[6];
  esp_fir_t filter_a;

  assert(esp_fir_init(&filter_a, &model, history_a, 6) == ESP_OK);
  assert(esp_fir_process_symmetric(&filter_a, 2.0f) == 2.0f);
  assert(esp_fir_process_symmetric(&filter_a, 2.0f) == 2.0f);

  assert(esp_fir_reset(&filter_a) == ESP_OK);
  assert(esp_fir_process_symmetric(&filter_a, -3.0f) == -3.0f);

  /* A second stream sharing the model must own a different history. */
  float history_b[6];
  esp_fir_t filter_b;

  assert(esp_fir_init(&filter_b, &model, history_b, 6) == ESP_OK);
  assert(esp_fir_process_symmetric(&filter_b, 1.0f) == 1.0f);
  assert(filter_a.history != filter_b.history);

  /* Failed preparation must not partially overwrite a valid destination. */
  const esp_fir_model_t saved_model = model;
  config.coefficient_count = 2;

  assert(esp_fir_prepare(&model, &config) != ESP_OK);
  assert(memcmp(&model, &saved_model, sizeof model) == 0);

  /* Reject a response that does not match the declared structure. */
  config.coefficient_count = 3;
  config.structure = ESP_FIR_ANTISYMMETRIC;
  assert(esp_fir_prepare(&model, &config) != ESP_OK);

  /* Reject invalid tolerance, layout, and startup enum values. */
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

  /* Reject undersized history and impossible history-size requests. */
  assert(esp_fir_init(&filter_a, &saved_model, history_a, 5) ==
         ESP_ERR_INVALID_SIZE);
  assert(esp_fir_history_size(0) == 0);
  assert(esp_fir_history_size((size_t)-1) == 0);

  puts("PASS startup priming, reset, independent states and validation");
}

/**
 * @brief Run all portable host tests for esp_fir.
 */
int main(void) {
  test_structures_and_layouts();
  test_high_order_filter();
  test_startup_lifecycle_and_validation();

  puts("All esp_fir host tests passed.");
  return 0;
}
