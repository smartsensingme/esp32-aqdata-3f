#include "esp_check.h"
#include "esp_fir.h"
#include "esp_log.h"

enum {
  EXAMPLE_TAP_COUNT = 5,
  EXAMPLE_HISTORY_FLOATS = 2 * EXAMPLE_TAP_COUNT,
};

static const char *TAG = "esp_fir_example";

/* Folded form of the symmetric response {0.1, 0.2, 0.4, 0.2, 0.1}. */
static const float coefficients[] = {0.1f, 0.2f, 0.4f};

static esp_fir_model_t model;
static esp_fir_t filter;
static float history[EXAMPLE_HISTORY_FLOATS];

/**
 * @brief Prepare the immutable model and initialize one stream.
 *
 * The coefficient array, model, state, and history have static lifetime because
 * esp_fir borrows their storage. First-sample priming models the signal as
 * constant before startup, so the first output equals input times the filter's
 * DC gain instead of including a zero-fill transient.
 */
static void initialize_filter(void) {
  const esp_fir_config_t config = {
      .coefficients = coefficients,
      .coefficient_count = sizeof coefficients / sizeof coefficients[0],
      .tap_count = EXAMPLE_TAP_COUNT,
      .structure = ESP_FIR_SYMMETRIC,
      .coefficient_layout = ESP_FIR_COEFFICIENTS_FOLDED,
      .startup = ESP_FIR_STARTUP_PRIME_FIRST_SAMPLE,
      .symmetry_tolerance = 0.0f,
  };

  ESP_ERROR_CHECK(esp_fir_prepare(&model, &config));
  ESP_ERROR_CHECK(
      esp_fir_init(&filter, &model, history, EXAMPLE_HISTORY_FLOATS));
}

/**
 * @brief Filter a short deterministic sequence and print each result.
 *
 * Real applications normally call esp_fir_process_symmetric() from their
 * periodic owner task and keep logging outside that timing-sensitive path.
 * This example logs inline only to make the standalone behavior observable.
 */
void app_main(void) {
  static const float samples[] = {1.0f, 1.0f, 0.0f, -1.0f, -1.0f};

  initialize_filter();

  for (size_t index = 0; index < sizeof samples / sizeof samples[0]; ++index) {
    const float output = esp_fir_process_symmetric(&filter, samples[index]);

    ESP_LOGI(TAG, "sample[%u]=%.3f output=%.3f", (unsigned)index,
             (double)samples[index], (double)output);
  }
}
