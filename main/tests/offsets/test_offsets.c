#include "app_offsets.h"
#include "fakes.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static app_offset_record_t stored, pending;
static bool present, low, fail_commit, fail_init;
static unsigned commits;
static float lut[4096], raw[3][APP_OFFSET_SAMPLE_COUNT],
    filtered[3][APP_OFFSET_SAMPLE_COUNT];

esp_err_t gpio_config(const gpio_config_t *cfg) {
  assert(cfg->pin_bit_mask == (1ULL << 7) &&
         cfg->pull_up_en == GPIO_PULLUP_ENABLE);
  return ESP_OK;
}
int gpio_get_level(int pin) {
  assert(pin == 7);
  return !low;
}
void vTaskDelay(unsigned ticks) { assert(ticks > 0); }
esp_err_t nvs_flash_init(void) { return fail_init ? ESP_FAIL : ESP_OK; }
esp_err_t nvs_open(const char *name, int mode, nvs_handle_t *h) {
  assert(!strcmp(name, "adc_offsets"));
  (void)mode;
  *h = 1;
  return ESP_OK;
}
esp_err_t nvs_get_blob(nvs_handle_t h, const char *key, void *data,
                       size_t *len) {
  (void)h;
  assert(!strcmp(key, "calibration_v2"));
  if (!present)
    return ESP_ERR_NVS_NOT_FOUND;
  if (*len < sizeof stored)
    return ESP_ERR_NVS_INVALID_LENGTH;
  memcpy(data, &stored, sizeof stored);
  *len = sizeof stored;
  return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t h, const char *key, const void *data,
                       size_t len) {
  (void)h;
  assert(!strcmp(key, "calibration_v2") && len == sizeof pending);
  memcpy(&pending, data, len);
  return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t h) {
  (void)h;
  ++commits;
  if (fail_commit)
    return ESP_FAIL;
  stored = pending;
  present = true;
  return ESP_OK;
}
void nvs_close(nvs_handle_t h) { (void)h; }
static void fill(void) {
  for (unsigned p = 0; p < 3; ++p)
    for (unsigned i = 0; i < APP_OFFSET_SAMPLE_COUNT; ++i) {
      raw[p][i] = 1.5f + p * 0.1f + (i % 2 ? 0.05f : -0.05f);
      filtered[p][i] = 1.5f + p * 0.1f;
    }
}
int main(void) {
  float offsets[3];
  assert(app_offsets_init(lut, offsets) == ESP_OK && commits == 0);
  for (unsigned p = 0; p < 3; ++p)
    assert(offsets[p] == 1.65f);
  low = true;
  assert(app_offsets_init(lut, offsets) == ESP_OK &&
         app_offsets_calibration_requested());
  fill();
  assert(app_offsets_commit_calibration(raw, filtered, offsets) == ESP_OK &&
         commits == 1);
  assert(!app_offsets_calibration_requested());
  assert(stored.version == 2 && stored.sample_count == 1440);
  for (unsigned p = 0; p < 3; ++p) {
    const float dc = 1.5f + p * 0.1f;
    assert(fabsf(offsets[p] - dc) < 1e-4f);
    assert(fabsf(stored.raw[p].mean - dc) < 1e-5f);
    assert(fabsf(stored.raw[p].rms - hypotf(dc, 0.05f)) < 1e-5f);
    assert(fabsf(stored.raw[p].standard_deviation - 0.05f) < 1e-5f);
    assert(fabsf(stored.raw[p].minimum - (dc - 0.05f)) < 1e-5f);
    assert(stored.filtered[p].standard_deviation == 0);
  }
  low = false;
  assert(app_offsets_init(lut, offsets) == ESP_OK &&
         !app_offsets_calibration_requested());
  assert(fabsf(offsets[2] - 1.7f) < 1e-4f);
  const float previous = offsets[0];
  fill();
  fail_commit = true;
  assert(app_offsets_commit_calibration(raw, filtered, offsets) == ESP_FAIL);
  assert(offsets[0] == previous && stored.offsets[0] == previous);
  fail_commit = false;
  raw[2][0] = NAN;
  assert(app_offsets_commit_calibration(raw, filtered, offsets) ==
         ESP_ERR_INVALID_RESPONSE);
  assert(commits == 2 && offsets[0] == previous);
  raw[2][0] = 0;
  assert(app_offsets_commit_calibration(raw, filtered, offsets) ==
         ESP_ERR_INVALID_RESPONSE);
  fill();
  // Outliers affect full statistics; trimmed mean still estimates the zero.
  for (unsigned i = 0; i < 100; ++i)
    filtered[0][i] = 0.1f;
  for (unsigned i = APP_OFFSET_SAMPLE_COUNT - 100; i < APP_OFFSET_SAMPLE_COUNT;
       ++i)
    filtered[0][i] = 3;
  assert(app_offsets_commit_calibration(raw, filtered, offsets) == ESP_OK);
  assert(fabsf(offsets[0] - 1.5f) < 1e-4f &&
         stored.filtered[0].standard_deviation > 0.1f);
  present = false;
  assert(app_offsets_init(lut, offsets) == ESP_OK && offsets[0] == 1.65f);
  present = true;
  stored.version = 1;
  assert(app_offsets_init(lut, offsets) == ESP_OK && offsets[0] == 1.65f);
  stored.version = 2;
  stored.offsets[1] = NAN;
  assert(app_offsets_init(lut, offsets) == ESP_OK && offsets[0] == 1.65f);
  fail_init = true;
  assert(app_offsets_init(lut, offsets) == ESP_FAIL);
  puts("Three-phase ADC/FIR offset statistics, persistence and defaults "
       "passed");
}
