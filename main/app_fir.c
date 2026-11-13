#include "app_fir.h"

#include "esp_attr.h"
#include "esp_check.h"

#define APP_FIR_TAPS 15U
#define APP_FIR_STORED_COEFFICIENTS 8U
#define APP_FIR_HISTORY_FLOATS (2U * APP_FIR_TAPS)

/* Folded Type-I FIR coefficients generated with Parks-McClellan/Remez.
 * Fs = 14400 Hz, order 14, group delay 7 samples. */
static DRAM_ATTR const float fir_coefficients[APP_FIR_STORED_COEFFICIENTS] = {
    -2.649769434988316e-02f, -1.973714929907618e-02f, -5.132740945614728e-03f,
    3.129857530427908e-02f,  8.558767456757677e-02f,  1.446596688184109e-01f,
    1.906123538202989e-01f,  2.079326067165584e-01f,
};

static DRAM_ATTR esp_fir_model_t fir_model;
esp_fir_t fir_fase_r;
esp_fir_t fir_fase_s;
esp_fir_t fir_fase_t;
static float fir_history_r[APP_FIR_HISTORY_FLOATS];
static float fir_history_s[APP_FIR_HISTORY_FLOATS];
static float fir_history_t[APP_FIR_HISTORY_FLOATS];

void app_fir_init(void) {
  const esp_fir_config_t config = {
      .coefficients = fir_coefficients,
      .coefficient_count = APP_FIR_STORED_COEFFICIENTS,
      .tap_count = APP_FIR_TAPS,
      .structure = ESP_FIR_SYMMETRIC,
      .coefficient_layout = ESP_FIR_COEFFICIENTS_FOLDED,
      .startup = ESP_FIR_STARTUP_PRIME_FIRST_SAMPLE,
      .symmetry_tolerance = 0.0f,
  };
  ESP_ERROR_CHECK(esp_fir_prepare(&fir_model, &config));
  ESP_ERROR_CHECK(esp_fir_init(&fir_fase_r, &fir_model, fir_history_r,
                               APP_FIR_HISTORY_FLOATS));
  ESP_ERROR_CHECK(esp_fir_init(&fir_fase_s, &fir_model, fir_history_s,
                               APP_FIR_HISTORY_FLOATS));
  ESP_ERROR_CHECK(esp_fir_init(&fir_fase_t, &fir_model, fir_history_t,
                               APP_FIR_HISTORY_FLOATS));
}

float app_fir_dc_gain(void) {
  float gain = fir_coefficients[APP_FIR_STORED_COEFFICIENTS - 1];
  for (unsigned i = 0; i < APP_FIR_STORED_COEFFICIENTS - 1; ++i)
    gain += 2.0f * fir_coefficients[i];
  return gain;
}
