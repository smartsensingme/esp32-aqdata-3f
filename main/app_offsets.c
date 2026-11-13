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
#define RECORD_VERSION 2U

_Static_assert(sizeof(app_offset_record_t) == 140, "NVS v2 layout changed");

static const char *TAG = "offsets";
static bool calibration_requested;

static bool valid_record(const app_offset_record_t *record) {
  if (record->version != RECORD_VERSION ||
      record->sample_count != APP_OFFSET_SAMPLE_COUNT)
    return false;
  for (unsigned p = 0; p < 3; ++p)
    if (!isfinite(record->offsets[p]) || record->offsets[p] <= 0.0f ||
        record->offsets[p] >= 3.1f)
      return false;
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

  app_offset_record_t saved = {0};
  size_t length = sizeof(saved);
  result = nvs_get_blob(nvs, "calibration_v2", &saved, &length);
  const bool loaded =
      result == ESP_OK && length == sizeof(saved) && valid_record(&saved);
  if (loaded) {
    for (unsigned phase = 0; phase < 3; ++phase)
      offsets[phase] = saved.offsets[phase];
  } else if (result != ESP_OK && result != ESP_ERR_NVS_NOT_FOUND &&
             result != ESP_ERR_NVS_INVALID_LENGTH) {
    nvs_close(nvs);
    return result;
  }
  if (loaded)
    ESP_LOGI(TAG, "Loaded offsets from NVS");
  else
    ESP_LOGW(TAG, "No valid offset record; using 1.65 V defaults");

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

bool app_offsets_calibration_requested(void) { return calibration_requested; }

/* Startup worker only: validate before accumulating; raw/FIR are in volts. */
static bool summarize(const float samples[APP_OFFSET_SAMPLE_COUNT],
                      app_offset_signal_report_t *report) {
  app_signal_stats_t stats = {0};
  float minimum = INFINITY, maximum = -INFINITY;
  for (unsigned i = 0; i < APP_OFFSET_SAMPLE_COUNT; ++i) {
    const float value = samples[i];
    if (!isfinite(value) || value <= 0 || value >= 3.1f)
      return false;
    app_signal_stats_add(&stats, value);
    minimum = fminf(minimum, value);
    maximum = fmaxf(maximum, value);
  }
  *report =
      (app_offset_signal_report_t){stats.mean, app_signal_rms(&stats),
                                   app_signal_ac_rms(&stats), minimum, maximum};
  return true;
}

/* Logging happens only after DMA stop, never in the acquisition loop. */
static void report_signal(const char *stage, unsigned phase,
                          const app_offset_signal_report_t *r) {
  ESP_LOGI(TAG,
           "Offset %s %c: DC/media=%.6f RMS=%.6f desvio_padrao=%.6f min=%.6f "
           "max=%.6f V; N=%u",
           stage, "RST"[phase], r->mean, r->rms, r->standard_deviation,
           r->minimum, r->maximum, APP_OFFSET_SAMPLE_COUNT);
}

esp_err_t
app_offsets_commit_calibration(const float raw[3][APP_OFFSET_SAMPLE_COUNT],
                               float filtered[3][APP_OFFSET_SAMPLE_COUNT],
                               float offsets[3]) {
  if (!raw || !filtered || !offsets)
    return ESP_ERR_INVALID_ARG;
  app_offset_record_t record = {.version = RECORD_VERSION,
                                .sample_count = APP_OFFSET_SAMPLE_COUNT};
  // Validate every phase before sorting, reporting or accessing NVS.
  for (unsigned p = 0; p < 3; ++p) {
    if (!summarize(raw[p], &record.raw[p]) ||
        !summarize(filtered[p], &record.filtered[p]))
      return ESP_ERR_INVALID_RESPONSE;
  }
  for (unsigned p = 0; p < 3; ++p) {
    report_signal("ADC", p, &record.raw[p]);
    report_signal("FIR", p, &record.filtered[p]);
    qsort(filtered[p], APP_OFFSET_SAMPLE_COUNT, sizeof(float), compare_float);
    float sum = 0;
    for (unsigned i = TRIM_COUNT; i < APP_OFFSET_SAMPLE_COUNT - TRIM_COUNT; ++i)
      sum += filtered[p][i];
    record.offsets[p] = sum / (APP_OFFSET_SAMPLE_COUNT - 2 * TRIM_COUNT);
  }
  nvs_handle_t nvs;
  esp_err_t result = nvs_open("adc_offsets", NVS_READWRITE, &nvs);
  if (result != ESP_OK)
    return result;
  result = nvs_set_blob(nvs, "calibration_v2", &record, sizeof(record));
  if (result == ESP_OK)
    result = nvs_commit(nvs);
  nvs_close(nvs);
  if (result == ESP_OK) {
    for (unsigned p = 0; p < 3; ++p)
      offsets[p] = record.offsets[p];
    calibration_requested = false;
    ESP_LOGI(TAG,
             "Calibration saved R/S/T (trimmed mean): %.6f / %.6f / %.6f V; "
             "release GPIO%d",
             offsets[0], offsets[1], offsets[2], CONFIG_APP_OFFSET_CAL_GPIO);
  }
  return result;
}
