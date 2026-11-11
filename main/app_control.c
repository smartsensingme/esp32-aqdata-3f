#include "app_control.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include <math.h>
#include <stdatomic.h>

#if CONFIG_APP_RMS_LOG_ENABLE
static const char *TAG = "app_control";
#endif

esp_pr_t pr_fase_r, pr_fase_s, pr_fase_t;

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

void app_control_init(void) {
  // Sintonia original do projeto, com Tustin prewarped por frequencia.
  const unsigned orders[] = {1, 3, 5, 7};
  const float gains[] = {60000.0f, 6000.0f, 10000.0f, 13000.0f};
  const float damping[] = {5.0f, 5.0f, 5.0f, 5.0f};
  enum { RESONATOR_COUNT = sizeof(orders) / sizeof(orders[0]) };
  esp_pr_resonator_config_t branches[RESONATOR_COUNT];
  ESP_ERROR_CHECK(esp_pr_make_harmonics(branches, RESONATOR_COUNT, 60.0f,
                                        orders, gains, damping));
  const esp_pr_config_t config = {
      .sample_rate_hz = 14400.0f,
      .kp = 5.0f,
      .output_min = CONTROL_OUTPUT_MIN_V,
      .output_max = CONTROL_OUTPUT_MAX_V,
      .anti_windup_tracking_time_s = CONFIG_APP_PR_TRACKING_TIME_US * 1e-6f,
      .resonators = branches,
      .resonator_count = RESONATOR_COUNT,
  };
  ESP_ERROR_CHECK(esp_pr_prepare(&pr_model, &config));
  ESP_ERROR_CHECK(esp_pr_init(&pr_fase_r, &pr_model));
  ESP_ERROR_CHECK(esp_pr_init(&pr_fase_s, &pr_model));
  ESP_ERROR_CHECK(esp_pr_init(&pr_fase_t, &pr_model));
}

float IRAM_ATTR app_control_process_phase(esp_pr_t *controller, float error) {
  float output = 0.0f;
  if (esp_pr_update(controller, error, 0.0f, &output) != ESP_OK) {
    // Falha numerica: recupera apenas esta fase e comanda 0 V bipolar.
    esp_pr_reset(controller);
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

bool IRAM_ATTR app_control_store_telemetry(uint16_t val_r, uint16_t val_s,
                                           uint16_t val_t) {
  uint32_t state = atomic_load_explicit(&capture_state, memory_order_acquire);
  if (state == CAPTURE_REQUESTED) {
    capture_idx = 0;
    atomic_store_explicit(&capture_state, CAPTURE_FILLING,
                          memory_order_relaxed);
  } else if (state != CAPTURE_FILLING) {
    return false;
  }

  dac_history_r[capture_idx] = val_r;
  dac_history_s[capture_idx] = val_s;
  dac_history_t[capture_idx] = val_t;
  if (++capture_idx == RMS_BUFFER_SIZE) {
    atomic_store_explicit(&capture_state, CAPTURE_READY, memory_order_release);
    return true;
  }
  return false;
}

bool app_control_calc_rms(float *out_rms_r, float *out_rms_s,
                          float *out_rms_t) {
  if (atomic_load_explicit(&capture_state, memory_order_acquire) !=
      CAPTURE_READY)
    return false;

  float sum_sq_r = 0.0f;
  float sum_sq_s = 0.0f;
  float sum_sq_t = 0.0f;

  for (int i = 0; i < RMS_BUFFER_SIZE; i++) {
    // R
    float v_cond_r =
        CONTROL_OUTPUT_MIN_V +
        (dac_history_r[i] * (CONTROL_OUTPUT_MAX_V - CONTROL_OUTPUT_MIN_V)) /
            DAC_MAX_CODE;
    float i_out_r = v_cond_r / R_LOAD;
    sum_sq_r += i_out_r * i_out_r;

    // S
    float v_cond_s =
        CONTROL_OUTPUT_MIN_V +
        (dac_history_s[i] * (CONTROL_OUTPUT_MAX_V - CONTROL_OUTPUT_MIN_V)) /
            DAC_MAX_CODE;
    float i_out_s = v_cond_s / R_LOAD;
    sum_sq_s += i_out_s * i_out_s;

    // T
    float v_cond_t =
        CONTROL_OUTPUT_MIN_V +
        (dac_history_t[i] * (CONTROL_OUTPUT_MAX_V - CONTROL_OUTPUT_MIN_V)) /
            DAC_MAX_CODE;
    float i_out_t = v_cond_t / R_LOAD;
    sum_sq_t += i_out_t * i_out_t;
  }

  float rms_r = N_COIN_TURNS * sqrtf(sum_sq_r / RMS_BUFFER_SIZE);
  float rms_s = N_COIN_TURNS * sqrtf(sum_sq_s / RMS_BUFFER_SIZE);
  float rms_t = N_COIN_TURNS * sqrtf(sum_sq_t / RMS_BUFFER_SIZE);

#if CONFIG_APP_RMS_LOG_ENABLE
  ESP_LOGI(TAG, "Corrente RMS Fase R: %.3f A", rms_r);
  ESP_LOGI(TAG, "Corrente RMS Fase S: %.3f A", rms_s);
  ESP_LOGI(TAG, "Corrente RMS Fase T: %.3f A", rms_t);
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
