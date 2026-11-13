#include "app_offsets.h"
#include "app_signal_stats.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include <math.h>
#include <stdint.h>
#include <stdlib.h>

#define TRIM_COUNT (APP_OFFSET_SAMPLE_COUNT / 10U)
#define RECORD_VERSION 1U

static const char *TAG = "offsets";
static bool calibration_requested;

typedef struct {
  uint32_t version;
  float volts[3];
} offset_record_t;

static bool valid_record(const offset_record_t *record) {
  if (record->version != RECORD_VERSION) {
    return false;
  }
  for (unsigned phase = 0; phase < 3; ++phase) {
    if (!isfinite(record->volts[phase]) || record->volts[phase] <= 0.0f ||
        record->volts[phase] >= 3.1f) {
      return false;
    }
  }
  return true;
}

static int compare_float(const void *a, const void *b) {
  const float x = *(const float *)a;
  const float y = *(const float *)b;
  return (x > y) - (x < y);
}

static esp_err_t offsets_startup(const float *adc_voltage_lut, float offsets[3],
                                 bool allow_calibration) {
  if (adc_voltage_lut == NULL || offsets == NULL) {
    return ESP_ERR_INVALID_ARG;
  }
  for (unsigned phase = 0; phase < 3; ++phase) {
    offsets[phase] = APP_OFFSET_DEFAULT_V;
  }

  const int pin = CONFIG_APP_OFFSET_CAL_GPIO;
  if (!GPIO_IS_VALID_GPIO(pin) || pin == CONFIG_MCP4922_CS1_PIN ||
      pin == CONFIG_MCP4922_CS2_PIN || pin == CONFIG_MCP4922_MOSI_PIN ||
      pin == CONFIG_MCP4922_SCK_PIN || pin == CONFIG_MCP4922_LDAC_PIN) {
    return ESP_ERR_INVALID_ARG;
  }
  const gpio_config_t gpio = {
      .pin_bit_mask = 1ULL << pin,
      .mode = GPIO_MODE_INPUT,
      .pull_up_en = GPIO_PULLUP_ENABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  esp_err_t gpio_result = gpio_config(&gpio);
  if (gpio_result != ESP_OK) {
    return gpio_result;
  }

  // Deliberately do not erase NVS on initialization errors: other namespaces
  // may contain application data. Such failures prevent starting the loop.
  esp_err_t result = nvs_flash_init();
  if (result != ESP_OK) {
    return result;
  }
  nvs_handle_t nvs;
  result = nvs_open("adc_offsets", NVS_READWRITE, &nvs);
  if (result != ESP_OK) {
    return result;
  }

  offset_record_t record = {0};
  size_t length = sizeof(record);
  result = nvs_get_blob(nvs, "record_v1", &record, &length);
  if (result == ESP_OK && length == sizeof(record) && valid_record(&record)) {
    for (unsigned phase = 0; phase < 3; ++phase) {
      offsets[phase] = record.volts[phase];
    }
    ESP_LOGI(TAG, "Loaded offsets from NVS");
  } else if (result == ESP_ERR_NVS_NOT_FOUND ||
             result == ESP_ERR_NVS_INVALID_LENGTH || result == ESP_OK) {
    ESP_LOGW(TAG, "No valid offset record; using 1.65 V defaults");
  } else {
    nvs_close(nvs);
    return result;
  }

  // Require three low samples, separated by at least 20 ms, to reject bounce.
  bool calibrate = allow_calibration;
  for (unsigned i = 0; i < 3; ++i) {
    vTaskDelay(pdMS_TO_TICKS(20));
    calibrate = calibrate && gpio_get_level(pin) == 0;
  }
  result = ESP_OK;
  calibration_requested = calibrate;
  if (calibrate)
    ESP_LOGW(TAG,
             "GPIO%d low: DMA offset calibration requested; zero primary "
             "current required",
             pin);
  nvs_close(nvs);
  ESP_LOGI(TAG, "Offsets R/S/T: %.6f / %.6f / %.6f V", offsets[0], offsets[1],
           offsets[2]);
  return result;
}

esp_err_t app_offsets_init(const float *lut, float offsets[3]) {
  return offsets_startup(lut, offsets, true);
}

esp_err_t app_offsets_load(const float *lut, float offsets[3]) {
  return offsets_startup(lut, offsets, false);
}

bool app_offsets_calibration_requested(void) { return calibration_requested; }

esp_err_t app_offsets_commit_samples(float samples[3][APP_OFFSET_SAMPLE_COUNT],
                                     unsigned mask, float offsets[3]) {
  if (!samples || !offsets || mask == 0 || mask > 7)
    return ESP_ERR_INVALID_ARG;
  offset_record_t record = {.version = RECORD_VERSION};
  for (unsigned phase = 0; phase < 3; ++phase) {
    record.volts[phase] = offsets[phase];
    if (!(mask & (1U << phase)))
      continue;
    for (unsigned i = 0; i < APP_OFFSET_SAMPLE_COUNT; ++i)
      if (!isfinite(samples[phase][i]) || samples[phase][i] <= 0 ||
          samples[phase][i] >= 3.1f)
        return ESP_ERR_INVALID_RESPONSE;
    app_signal_stats_t signal = {0};
    for (unsigned i = 0; i < APP_OFFSET_SAMPLE_COUNT; ++i)
      app_signal_stats_add(&signal, samples[phase][i]);
    ESP_LOGI(TAG, "Calibracao FIR %c: media=%.6f RMS=%.6f V; ruido_RMS=%.3f mV",
             "RST"[phase], signal.mean, app_signal_rms(&signal),
             app_signal_ac_rms(&signal) * 1000.0f);
    qsort(samples[phase], APP_OFFSET_SAMPLE_COUNT, sizeof(float),
          compare_float);
    float sum = 0;
    for (unsigned i = TRIM_COUNT; i < APP_OFFSET_SAMPLE_COUNT - TRIM_COUNT; ++i)
      sum += samples[phase][i];
    record.volts[phase] = sum / (APP_OFFSET_SAMPLE_COUNT - 2 * TRIM_COUNT);
  }
  return app_offsets_commit_values(record.volts, mask, offsets);
}

esp_err_t app_offsets_commit_values(const float values[3], unsigned mask,
                                    float offsets[3]) {
  if (!values || !offsets || mask == 0 || mask > 7)
    return ESP_ERR_INVALID_ARG;
  offset_record_t record = {.version = RECORD_VERSION};
  for (unsigned phase = 0; phase < 3; ++phase)
    record.volts[phase] = mask & (1U << phase) ? values[phase] : offsets[phase];
  if (!valid_record(&record))
    return ESP_ERR_INVALID_RESPONSE;
  nvs_handle_t nvs;
  esp_err_t result = nvs_open("adc_offsets", NVS_READWRITE, &nvs);
  if (result != ESP_OK)
    return result;
  result = nvs_set_blob(nvs, "record_v1", &record, sizeof(record));
  if (result == ESP_OK)
    result = nvs_commit(nvs);
  nvs_close(nvs);
  if (result == ESP_OK) {
    for (unsigned phase = 0; phase < 3; ++phase)
      offsets[phase] = record.volts[phase];
    calibration_requested = false;
    ESP_LOGI(
        TAG,
        "Calibration saved (DMA) R/S/T: %.6f / %.6f / %.6f V; release GPIO%d",
        offsets[0], offsets[1], offsets[2], CONFIG_APP_OFFSET_CAL_GPIO);
  }
  return result;
}
