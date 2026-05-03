#include "app_control.h"
#include <math.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "app_control";

PR_Estado_t pr_fase_r, pr_fase_s, pr_fase_t;

DRAM_ATTR uint16_t sine_lut[SINE_POINTS];

volatile bool capture_requested = false;
volatile int capture_idx = 0;
static uint16_t dac_history_r[RMS_BUFFER_SIZE];
static uint16_t dac_history_s[RMS_BUFFER_SIZE];
static uint16_t dac_history_t[RMS_BUFFER_SIZE];

static void pr_init(PR_Estado_t *estado) {
  for (int i = 0; i < 3; i++) {
    estado->e[i] = 0.0f;
    estado->u[i] = 0.0f;
  }
}

void app_control_init(void) {
  pr_init(&pr_fase_r);
  pr_init(&pr_fase_s);
  pr_init(&pr_fase_t);

  for (int i = 0; i < SINE_POINTS; i++) {
    float angle = (2.0f * M_PI * i) / SINE_POINTS;
    float sin_val = sinf(angle);
    sine_lut[i] = (uint16_t)((sin_val + 1.0f) * 2047.5f);
  }
}

float IRAM_ATTR calcular_controle_PR(PR_Estado_t *estado, float erro_atual) {
  estado->e[0] = erro_atual;

  estado->u[0] = (PR_B0 * estado->e[0]) + (PR_B1 * estado->e[1]) +
                 (PR_B2 * estado->e[2]) - (PR_A1 * estado->u[1]) -
                 (PR_A2 * estado->u[2]) + PR_OFFSET;

  if (estado->u[0] > 4095.0f)
    estado->u[0] = 4095.0f;
  if (estado->u[0] < 0.0f)
    estado->u[0] = 0.0f;

  estado->e[2] = estado->e[1];
  estado->e[1] = estado->e[0];
  estado->u[2] = estado->u[1];
  estado->u[1] = estado->u[0];

  return estado->u[0];
}

bool IRAM_ATTR app_control_store_telemetry(uint16_t val_r, uint16_t val_s, uint16_t val_t) {
  if (capture_requested) {
    dac_history_r[capture_idx] = val_r;
    dac_history_s[capture_idx] = val_s;
    dac_history_t[capture_idx] = val_t;
    capture_idx++;
    if (capture_idx >= RMS_BUFFER_SIZE) {
      capture_requested = false;
      return true;
    }
  }
  return false;
}

void app_control_calc_rms(void) {
  float sum_sq_r = 0.0f;
  float sum_sq_s = 0.0f;
  float sum_sq_t = 0.0f;

  for (int i = 0; i < RMS_BUFFER_SIZE; i++) {
    // R
    float v_dac_r = (dac_history_r[i] * 3.3f) / 4095.0f;
    float v_cond_r = (v_dac_r * OUT_GAIN) + OUT_OFFSET;
    float i_out_r = v_cond_r / R_LOAD;
    sum_sq_r += i_out_r * i_out_r;

    // S
    float v_dac_s = (dac_history_s[i] * 3.3f) / 4095.0f;
    float v_cond_s = (v_dac_s * OUT_GAIN) + OUT_OFFSET;
    float i_out_s = v_cond_s / R_LOAD;
    sum_sq_s += i_out_s * i_out_s;

    // T
    float v_dac_t = (dac_history_t[i] * 3.3f) / 4095.0f;
    float v_cond_t = (v_dac_t * OUT_GAIN) + OUT_OFFSET;
    float i_out_t = v_cond_t / R_LOAD;
    sum_sq_t += i_out_t * i_out_t;
  }

  float rms_r = N_COIN_TURNS * sqrtf(sum_sq_r / RMS_BUFFER_SIZE);
  float rms_s = N_COIN_TURNS * sqrtf(sum_sq_s / RMS_BUFFER_SIZE);
  float rms_t = N_COIN_TURNS * sqrtf(sum_sq_t / RMS_BUFFER_SIZE);

  ESP_LOGI(TAG, "Corrente RMS Fase R: %.3f A", rms_r);
  ESP_LOGI(TAG, "Corrente RMS Fase S: %.3f A", rms_s);
  ESP_LOGI(TAG, "Corrente RMS Fase T: %.3f A", rms_t);
}
