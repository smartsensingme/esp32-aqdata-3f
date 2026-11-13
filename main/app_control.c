#include "app_control.h"
#include "app_signal_stats.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include <math.h>
#include <stdatomic.h>

#if CONFIG_APP_RMS_LOG_ENABLE
static const char *TAG = "app_control";
#endif

DRAM_ATTR app_parallel_t pr_fase_r, pr_fase_s, pr_fase_t;

// Coeficientes compartilhados em RAM interna; estados separados por fase.
static DRAM_ATTR esp_pr_model_t pr_model;

/* Control task owns the index and writes samples. The RMS worker publishes
 * requests and only reads buffers after the release/acquire READY handoff. */
enum { CAPTURE_IDLE, CAPTURE_REQUESTED, CAPTURE_FILLING, CAPTURE_READY };
static _Atomic uint32_t capture_state;
static uint32_t capture_idx;
_Static_assert(__atomic_always_lock_free(sizeof(uint32_t), 0),
               "Capture handoff must use lock-free 32-bit atomics");
static uint16_t dac_history_r[RMS_BUFFER_SIZE];
static uint16_t dac_history_s[RMS_BUFFER_SIZE];
static uint16_t dac_history_t[RMS_BUFFER_SIZE];
static float adc_history[3][RMS_BUFFER_SIZE];
static float filtered_history[3][RMS_BUFFER_SIZE];
static bool signal_capture_valid;

void app_control_init(void) {
  app_control_init_with_limit(CONFIG_APP_CONTROL_LIMIT_MV / 1000.0f);
}

void app_control_init_with_limit(float limit) {
  ESP_ERROR_CHECK(isfinite(limit) && limit > 0.0f &&
                          limit <= CONTROL_OUTPUT_MAX_V
                      ? ESP_OK
                      : ESP_ERR_INVALID_ARG);
  // Sintonia provisoria de comissionamento, com Tustin prewarped.
  const unsigned orders[] = {1, 2, 3, 5, 7};
  const float gains[] = {APP_CONTROL_KR60, APP_CONTROL_KR120, APP_CONTROL_KR180,
                         APP_CONTROL_KR300, APP_CONTROL_KR420};
  const float damping[] = {5.0f, 5.0f, 5.0f, 5.0f, 5.0f};
  enum { RESONATOR_COUNT = sizeof(orders) / sizeof(orders[0]) };
  esp_pr_resonator_config_t branches[RESONATOR_COUNT];
  ESP_ERROR_CHECK(esp_pr_make_harmonics(branches, RESONATOR_COUNT, 60.0f,
                                        orders, gains, damping));
  const esp_pr_config_t config = {
      .sample_rate_hz = 14400.0f,
      .kp = APP_CONTROL_KP,
      .output_min = -limit,
      .output_max = limit,
      .anti_windup_tracking_time_s =
          CONFIG_APP_PR_TRACKING_TIME_US / 1000000.0f,
      .tracking_mode = ESP_PR_TRACKING_EXTERNAL,
      .resonators = branches,
      .resonator_count = RESONATOR_COUNT,
  };
  ESP_ERROR_CHECK(esp_pr_prepare(&pr_model, &config));
  ESP_ERROR_CHECK(
      app_parallel_init(&pr_fase_r, &pr_model, APP_CONTROL_KI, limit));
  ESP_ERROR_CHECK(
      app_parallel_init(&pr_fase_s, &pr_model, APP_CONTROL_KI, limit));
  ESP_ERROR_CHECK(
      app_parallel_init(&pr_fase_t, &pr_model, APP_CONTROL_KI, limit));
}

float IRAM_ATTR app_control_process_phase(app_parallel_t *controller,
                                          float error) {
  float output = 0.0f;
  if (app_parallel_update(controller, error, &output) != ESP_OK) {
    // Falha numerica: recupera apenas esta fase e comanda 0 V bipolar.
    app_parallel_reset(controller);
    return 0.0f;
  }
  return output;
}

uint16_t IRAM_ATTR app_control_voltage_to_dac(float control_voltage) {
  if (!isfinite(control_voltage)) {
    control_voltage = 0.0f;
  }
  if (control_voltage > CONTROL_OUTPUT_MAX_V) {
    control_voltage = CONTROL_OUTPUT_MAX_V;
  } else if (control_voltage < CONTROL_OUTPUT_MIN_V) {
    control_voltage = CONTROL_OUTPUT_MIN_V;
  }

  const float normalized = (control_voltage - CONTROL_OUTPUT_MIN_V) /
                           (CONTROL_OUTPUT_MAX_V - CONTROL_OUTPUT_MIN_V);
  return (uint16_t)(normalized * (float)DAC_MAX_CODE + 0.5f);
}

bool app_control_request_capture(void) {
  uint32_t expected = CAPTURE_IDLE;
  return atomic_compare_exchange_strong_explicit(
      &capture_state, &expected, CAPTURE_REQUESTED, memory_order_release,
      memory_order_relaxed);
}

bool IRAM_ATTR app_control_store_telemetry(uint16_t r, uint16_t s, uint16_t t) {
  const uint16_t commands[3] = {r, s, t};
  return app_control_store_sample(commands, NULL, NULL);
}

bool IRAM_ATTR app_control_store_sample(const uint16_t commands[3],
                                        const float adc[3],
                                        const float filtered[3]) {
  uint32_t state = atomic_load_explicit(&capture_state, memory_order_acquire);
  if (state == CAPTURE_REQUESTED) {
    capture_idx = 0;
    signal_capture_valid = adc && filtered;
    atomic_store_explicit(&capture_state, CAPTURE_FILLING,
                          memory_order_relaxed);
  } else if (state != CAPTURE_FILLING) {
    return false;
  }

  dac_history_r[capture_idx] = commands[0];
  dac_history_s[capture_idx] = commands[1];
  dac_history_t[capture_idx] = commands[2];
  if (adc && filtered) {
    for (unsigned phase = 0; phase < 3; ++phase) {
      adc_history[phase][capture_idx] = adc[phase];
      filtered_history[phase][capture_idx] = filtered[phase];
    }
  } else
    signal_capture_valid = false;
  if (++capture_idx == RMS_BUFFER_SIZE) {
    atomic_store_explicit(&capture_state, CAPTURE_READY, memory_order_release);
    return true;
  }
  return false;
}

bool app_control_calc_rms_report(float *out_rms_r, float *out_rms_s,
                                 float *out_rms_t, bool detailed) {
  if (atomic_load_explicit(&capture_state, memory_order_acquire) !=
      CAPTURE_READY)
    return false;

  float sum_sq_r = 0.0f;
  float sum_sq_s = 0.0f;
  float sum_sq_t = 0.0f;

  for (int i = 0; i < RMS_BUFFER_SIZE; i++) {
    const float i_r = app_control_dac_to_primary_current(dac_history_r[i]);
    const float i_s = app_control_dac_to_primary_current(dac_history_s[i]);
    const float i_t = app_control_dac_to_primary_current(dac_history_t[i]);
    sum_sq_r += i_r * i_r;
    sum_sq_s += i_s * i_s;
    sum_sq_t += i_t * i_t;
  }

  float rms_r = sqrtf(sum_sq_r / RMS_BUFFER_SIZE);
  float rms_s = sqrtf(sum_sq_s / RMS_BUFFER_SIZE);
  float rms_t = sqrtf(sum_sq_t / RMS_BUFFER_SIZE);

#if CONFIG_APP_RMS_LOG_ENABLE
  if (detailed)
    ESP_LOGI(TAG,
             "Relatorio R: N=%u janela=100 ms; media=DC; RMS total inclui DC; "
             "desvio populacional=RMS AC",
             (unsigned)RMS_BUFFER_SIZE);
  const float currents[3] = {rms_r, rms_s, rms_t};
  const uint16_t *histories[3] = {dac_history_r, dac_history_s, dac_history_t};
  for (unsigned phase = 0; phase < 3; ++phase) {
    const char name = "RST"[phase];
    app_signal_stats_t current = {0}, dac = {0};
    uint16_t code_min = DAC_MAX_CODE, code_max = 0;
    for (unsigned i = 0; i < RMS_BUFFER_SIZE; ++i) {
      const uint16_t code = histories[phase][i];
      app_signal_stats_add(&current, app_control_dac_to_primary_current(code));
      app_signal_stats_add(&dac, (float)code * (DAC_REFERENCE_V / 4096.0f));
      if (code < code_min)
        code_min = code;
      if (code > code_max)
        code_max = code;
    }
    ESP_LOGI(TAG,
             "Compensacao estimada %c: Icomp_mean=%+.6f Icomp_rms=%.6f A; "
             "Icomp_ac_rms=%.6f A",
             name, current.mean, currents[phase], app_signal_ac_rms(&current));
    if (detailed && phase == 0) {
      ESP_LOGI(
          TAG,
          "DAC %c (estimado): media=%.6f RMS=%.6f V; code_min=%u code_max=%u",
          name, dac.mean, app_signal_rms(&dac), (unsigned)code_min,
          (unsigned)code_max);
      if (signal_capture_valid) {
        app_signal_stats_t raw = {0}, filtered = {0};
        float raw_min = INFINITY, raw_max = -INFINITY;
        float fir_min = INFINITY, fir_max = -INFINITY;
        for (unsigned i = 0; i < RMS_BUFFER_SIZE; ++i) {
          app_signal_stats_add(&raw, adc_history[phase][i]);
          app_signal_stats_add(&filtered, filtered_history[phase][i]);
          raw_min = fminf(raw_min, adc_history[phase][i]);
          raw_max = fmaxf(raw_max, adc_history[phase][i]);
          fir_min = fminf(fir_min, filtered_history[phase][i]);
          fir_max = fmaxf(fir_max, filtered_history[phase][i]);
        }
        ESP_LOGI(TAG, "ADC %c: media=%.6f RMS=%.6f V; desvio_padrao=%.3f mV",
                 name, raw.mean, app_signal_rms(&raw),
                 1000.0f * app_signal_ac_rms(&raw));
        ESP_LOGI(TAG,
                 "FIR %c (DC normalizado): media=%.6f RMS=%.6f V; "
                 "desvio_padrao=%.3f mV",
                 name, filtered.mean, app_signal_rms(&filtered),
                 1000.0f * app_signal_ac_rms(&filtered));
        ESP_LOGI(TAG, "R min/max: ADC=%.6f/%.6f FIR=%.6f/%.6f V", raw_min,
                 raw_max, fir_min, fir_max);
      }
    }
  }
#else
  (void)detailed;
#endif

  if (out_rms_r)
    *out_rms_r = rms_r;
  if (out_rms_s)
    *out_rms_s = rms_s;
  if (out_rms_t)
    *out_rms_t = rms_t;

  /* The next request cannot be accepted until the consumer is finished. */
  atomic_store_explicit(&capture_state, CAPTURE_IDLE, memory_order_release);
  return true;
}

/* Compatibility entry point for numerical consumers; periodic reporting uses
 * the explicit detail flag so log cadence does not depend on call count. */
bool app_control_calc_rms(float *r, float *s, float *t) {
  return app_control_calc_rms_report(r, s, t, false);
}
