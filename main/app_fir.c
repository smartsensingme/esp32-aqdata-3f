#include "app_fir.h"

#include "esp_attr.h"
#include "esp_check.h"
#include "esp_clk_tree.h"
#include "esp_cpu.h"
#include "esp_log.h"
#include "fir_pmr.h"
#include "sdkconfig.h"
#include "soc/clk_tree_defs.h"

#include <math.h>
#include <stdint.h>

#define APP_FIR_TAPS 15U
#define APP_FIR_STORED_COEFFICIENTS 8U
#define APP_FIR_HISTORY_FLOATS (2U * APP_FIR_TAPS)

#if CONFIG_APP_FIR_BENCHMARK
static const char *TAG = "app_fir";
#endif

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

#if CONFIG_APP_FIR_BENCHMARK
#define BENCHMARK_SAMPLES 256U
#define BENCHMARK_PASSES 16U
#define BENCHMARK_ROUNDS 9U

static DRAM_ATTR float benchmark_samples[BENCHMARK_SAMPLES];
static volatile float benchmark_sink;

typedef struct {
  uint32_t specific_cycles;
  uint32_t generic_cycles;
  uint32_t frames;
  uint32_t cpu_frequency_hz;
  float max_error;
  bool valid;
} fir_benchmark_result_t;
static fir_benchmark_result_t benchmark_result;

static void make_benchmark_samples(void) {
  uint32_t random = 0x6d2b79f5U;
  for (size_t i = 0; i < BENCHMARK_SAMPLES; ++i) {
    random = random * 1664525U + 1013904223U;
    benchmark_samples[i] =
        (float)((int32_t)(random >> 8) - 0x7fffff) / 8388608.0f;
  }
}

static uint32_t benchmark_specific(void) {
  static fir_pmr_state_t filters[3];
  for (size_t phase = 0; phase < 3; ++phase)
    fir_pmr_init(&filters[phase]);
  float sum = 0.0f;
  const uint32_t start = esp_cpu_get_cycle_count();
  for (size_t pass = 0; pass < BENCHMARK_PASSES; ++pass) {
    for (size_t i = 0; i < BENCHMARK_SAMPLES; ++i) {
      const float x = benchmark_samples[i];
      sum += fir_pmr_process(&filters[0], x);
      sum += fir_pmr_process(&filters[1], x * .75f);
      sum += fir_pmr_process(&filters[2], x * -.5f);
    }
  }
  const uint32_t cycles = esp_cpu_get_cycle_count() - start;
  benchmark_sink = sum;
  return cycles;
}

static uint32_t benchmark_generic(void) {
  static esp_fir_t filters[3];
  static float histories[3][APP_FIR_HISTORY_FLOATS];
  for (size_t phase = 0; phase < 3; ++phase) {
    ESP_ERROR_CHECK(esp_fir_init(&filters[phase], &fir_model, histories[phase],
                                 APP_FIR_HISTORY_FLOATS));
  }
  float sum = 0.0f;
  const uint32_t start = esp_cpu_get_cycle_count();
  for (size_t pass = 0; pass < BENCHMARK_PASSES; ++pass) {
    for (size_t i = 0; i < BENCHMARK_SAMPLES; ++i) {
      const float x = benchmark_samples[i];
      sum += esp_fir_process_symmetric(&filters[0], x);
      sum += esp_fir_process_symmetric(&filters[1], x * .75f);
      sum += esp_fir_process_symmetric(&filters[2], x * -.5f);
    }
  }
  const uint32_t cycles = esp_cpu_get_cycle_count() - start;
  benchmark_sink = sum;
  return cycles;
}

static float compare_outputs(void) {
  fir_pmr_state_t specific;
  esp_fir_t generic;
  float history[APP_FIR_HISTORY_FLOATS];
  fir_pmr_init(&specific);
  ESP_ERROR_CHECK(
      esp_fir_init(&generic, &fir_model, history, APP_FIR_HISTORY_FLOATS));
  float max_error = 0.0f;
  for (size_t pass = 0; pass < BENCHMARK_PASSES; ++pass) {
    for (size_t i = 0; i < BENCHMARK_SAMPLES; ++i) {
      const float old_y = fir_pmr_process(&specific, benchmark_samples[i]);
      const float new_y =
          esp_fir_process_symmetric(&generic, benchmark_samples[i]);
      const float error = fabsf(old_y - new_y);
      if (error > max_error)
        max_error = error;
    }
  }
  return max_error;
}
#endif

void app_fir_benchmark(void) {
#if CONFIG_APP_FIR_BENCHMARK
  make_benchmark_samples();
  uint32_t best_specific = UINT32_MAX;
  uint32_t best_generic = UINT32_MAX;
  for (size_t round = 0; round < BENCHMARK_ROUNDS; ++round) {
    uint32_t specific;
    uint32_t generic;
    /* Alternate execution order so neither implementation always receives the
     * same cache/warm-up advantage. The minimum rejects interrupted rounds. */
    if (round & 1U) {
      generic = benchmark_generic();
      specific = benchmark_specific();
    } else {
      specific = benchmark_specific();
      generic = benchmark_generic();
    }
    if (specific < best_specific)
      best_specific = specific;
    if (generic < best_generic)
      best_generic = generic;
  }
  uint32_t cpu_frequency_hz = 0;
  if (esp_clk_tree_src_get_freq_hz(SOC_MOD_CLK_CPU,
                                   ESP_CLK_TREE_SRC_FREQ_PRECISION_CACHED,
                                   &cpu_frequency_hz) != ESP_OK) {
    cpu_frequency_hz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ * 1000000U;
  }
  benchmark_result = (fir_benchmark_result_t){
      .specific_cycles = best_specific,
      .generic_cycles = best_generic,
      .frames = BENCHMARK_SAMPLES * BENCHMARK_PASSES,
      .cpu_frequency_hz = cpu_frequency_hz,
      .max_error = compare_outputs(),
      .valid = true,
  };
  app_fir_report_benchmark();
#endif
}

void app_fir_report_benchmark(void) {
#if CONFIG_APP_FIR_BENCHMARK
  if (!benchmark_result.valid)
    return;
  const double specific_per_frame =
      (double)benchmark_result.specific_cycles / benchmark_result.frames;
  const double generic_per_frame =
      (double)benchmark_result.generic_cycles / benchmark_result.frames;
  const double difference = generic_per_frame - specific_per_frame;
  const double difference_percent = 100.0 * difference / specific_per_frame;
  const double cycles_per_us = benchmark_result.cpu_frequency_hz / 1000000.0;
  ESP_LOGI(TAG,
           "FIR COST (3 phases, best of %u): specific=%.2f cycles/frame "
           "(%.2f/call, %.3fus), esp_fir=%.2f cycles/frame (%.2f/call, "
           "%.3fus), delta=%+.2f cycles/frame (%+.2f%%), max_error=%.9g, "
           "cpu=%.0fMHz",
           BENCHMARK_ROUNDS, specific_per_frame, specific_per_frame / 3.0,
           specific_per_frame / cycles_per_us, generic_per_frame,
           generic_per_frame / 3.0, generic_per_frame / cycles_per_us,
           difference, difference_percent, (double)benchmark_result.max_error,
           benchmark_result.cpu_frequency_hz / 1000000.0);
#endif
}
