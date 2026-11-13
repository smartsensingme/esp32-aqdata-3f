#include "app_polarity.h"
#include "app_control.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>

#define TEST_CYCLES 5U
#define SAMPLE_BLOCKS 20U
#define SAMPLES_PER_BLOCK 16U
#define SETTLE_MS 300U
#define MIN_RESPONSE_V 0.005f

static const char *TAG = "polarity";

typedef struct {
  float mean;
  float variance;
} measurement_t;

/* About 200 ms/window at the current 100 Hz tick: averages over many 60 Hz
 * cycles. Variance retains signal/noise within the window, not just SEM;
 * correlated interference must not receive an artificially small threshold. */
static esp_err_t measure(adc_oneshot_unit_handle_t adc, const float *lut,
                         measurement_t *measurement) {
  float mean = 0.0f;
  float m2 = 0.0f;
  unsigned count = 0;
  TickType_t release = xTaskGetTickCount();
  const TickType_t interval = pdMS_TO_TICKS(10) > 0 ? pdMS_TO_TICKS(10) : 1;

  for (unsigned block = 0; block < SAMPLE_BLOCKS; ++block) {
    for (unsigned i = 0; i < SAMPLES_PER_BLOCK; ++i) {
      int raw = 0;
      esp_err_t result = adc_oneshot_read(adc, ADC_CHANNEL_3, &raw);
      if (result != ESP_OK) {
        return result;
      }
      if (raw < 32 || raw > 4063 || !isfinite(lut[raw]) || lut[raw] < 0.1f ||
          lut[raw] > 3.0f) {
        return ESP_ERR_INVALID_RESPONSE;
      }
      const float value = lut[raw];
      const float delta = value - mean;
      mean += delta / (float)++count;
      m2 += delta * (value - mean);
    }
    xTaskDelayUntil(&release, interval);
  }
  measurement->mean = mean;
  measurement->variance = fmaxf(0.0f, m2 / (float)(count - 1));
  return ESP_OK;
}

void app_polarity_run(mcp4922_context_t *dac, const float *lut, unsigned step) {
  if (dac == NULL || lut == NULL || step == 0 ||
      step > DAC_MAX_CODE - DAC_NEUTRAL_CODE) {
    ESP_LOGE(TAG, "Invalid polarity-test arguments");
    return;
  }

  uint16_t values[4] = {DAC_NEUTRAL_CODE, DAC_NEUTRAL_CODE, DAC_NEUTRAL_CODE,
                        DAC_NEUTRAL_CODE};
  adc_oneshot_unit_handle_t adc = NULL;
  const adc_oneshot_unit_init_cfg_t unit = {.unit_id = ADC_UNIT_1};
  const adc_oneshot_chan_cfg_t channel = {.atten = ADC_ATTEN_DB_12,
                                          .bitwidth = ADC_BITWIDTH_12};
  bool dac_failed = false;
  bool conclusive = true;
  int direction = 0;
  esp_err_t result = mcp4922_write_channels(dac, values);
  if (result != ESP_OK) {
    ESP_LOGE(TAG, "DAC initialization write failed: %s; reset required",
             esp_err_to_name(result));
    return;
  }

  result = adc_oneshot_new_unit(&unit, &adc);
  if (result == ESP_OK) {
    result = adc_oneshot_config_channel(adc, ADC_CHANNEL_3, &channel);
  }
  ESP_LOGW(TAG,
           "AUTOMATIC POLARITY TEST R: DAC %u -> %u; keep primary current "
           "constant (zero preferred)",
           (unsigned)DAC_NEUTRAL_CODE, (unsigned)(DAC_NEUTRAL_CODE + step));
  ESP_LOGW(TAG,
           "PR/NVS off; 5 cycles, 300 ms settling + 200 ms ADC per plateau");
  vTaskDelay(pdMS_TO_TICKS(2000));

  for (unsigned cycle = 0; cycle < TEST_CYCLES && result == ESP_OK; ++cycle) {
    measurement_t before, high, after;
    vTaskDelay(pdMS_TO_TICKS(SETTLE_MS));
    result = measure(adc, lut, &before);
    if (result != ESP_OK) {
      break;
    }

    values[0] = DAC_NEUTRAL_CODE + step;
    result = mcp4922_write_channels(dac, values);
    if (result != ESP_OK) {
      dac_failed = true;
      break;
    }
    vTaskDelay(pdMS_TO_TICKS(SETTLE_MS));
    result = measure(adc, lut, &high);

    // Return to neutral even when the ADC fails during the positive plateau.
    values[0] = DAC_NEUTRAL_CODE;
    const esp_err_t restore = mcp4922_write_channels(dac, values);
    if (restore != ESP_OK) {
      result = restore;
      dac_failed = true;
      break;
    }
    if (result != ESP_OK) {
      break;
    }
    vTaskDelay(pdMS_TO_TICKS(SETTLE_MS));
    result = measure(adc, lut, &after);
    if (result != ESP_OK) {
      break;
    }

    const float response = high.mean - 0.5f * (before.mean + after.mean);
    const float noise =
        sqrtf((before.variance + high.variance + after.variance) / 3.0f);
    const float threshold = fmaxf(MIN_RESPONSE_V, 3.0f * noise);
    const float drift = fabsf(after.mean - before.mean);
    const int sign = response > 0.0f ? 1 : -1;
    ESP_LOGI(TAG,
             "Cycle %u: ADC neutral %.4f -> high %.4f -> neutral %.4f V; "
             "delta %+.2f mV, threshold %.2f mV, drift %.2f mV",
             cycle + 1, before.mean, high.mean, after.mean, response * 1000.0f,
             threshold * 1000.0f, drift * 1000.0f);
    ESP_LOGI(TAG,
             "ADC RMS: neutral_before=%.6f high=%.6f neutral_after=%.6f V; "
             "ruido_RMS=%.3f mV",
             sqrtf(before.mean * before.mean + before.variance),
             sqrtf(high.mean * high.mean + high.variance),
             sqrtf(after.mean * after.mean + after.variance), noise * 1000.0f);
    if (fabsf(response) <= threshold || drift > threshold ||
        (direction != 0 && sign != direction)) {
      conclusive = false;
      break;
    }
    direction = sign;
  }

  if (adc != NULL) {
    const esp_err_t cleanup = adc_oneshot_del_unit(adc);
    if (result == ESP_OK) {
      result = cleanup;
    }
  }
  if (result != ESP_OK) {
    ESP_LOGE(TAG, "INCONCLUSIVE: peripheral/ADC range error: %s",
             esp_err_to_name(result));
  } else if (!conclusive || direction == 0) {
    ESP_LOGW(TAG,
             "INCONCLUSIVE: weak/noisy response, drift or inconsistent sign");
  } else if (direction < 0) {
    ESP_LOGI(TAG, "COMPATIBLE: DAC increases, GPIO4 decreases (negative "
                  "feedback with current PR sign)");
  } else {
    ESP_LOGW(TAG, "INVERTED: DAC increases, GPIO4 increases; invert coil OR "
                  "controller sign before closing loop");
  }
  if (dac_failed) {
    ESP_LOGE(
        TAG,
        "DAC fault: output may retain last value; no retry, reset required");
  } else {
    ESP_LOGI(TAG, "Test finished; all DACs neutral. Control remains stopped "
                  "until reset/reflash");
  }
}
