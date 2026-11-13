#include "app_offsets.h"
#include "fakes.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static struct {
  uint32_t version;
  float volts[3];
} stored, pending;
static bool present, low, fail_commit, fail_init;
static unsigned commits;
static float lut[4096];

esp_err_t gpio_config(const gpio_config_t *cfg) {
  assert(cfg->pin_bit_mask == (1ULL << 7));
  assert(cfg->pull_up_en == GPIO_PULLUP_ENABLE);
  return ESP_OK;
}
int gpio_get_level(int pin) {
  assert(pin == 7);
  return !low;
}
void vTaskDelay(unsigned ticks) { assert(ticks > 0); }
esp_err_t nvs_flash_init(void) { return fail_init ? ESP_FAIL : ESP_OK; }
esp_err_t nvs_open(const char *name, int mode, nvs_handle_t *handle) {
  assert(strcmp(name, "adc_offsets") == 0);
  (void)mode;
  *handle = 1;
  return ESP_OK;
}
esp_err_t nvs_get_blob(nvs_handle_t h, const char *key, void *data,
                       size_t *len) {
  (void)h;
  (void)key;
  if (!present)
    return ESP_ERR_NVS_NOT_FOUND;
  assert(*len == sizeof(stored));
  memcpy(data, &stored, *len);
  return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t h, const char *key, const void *data,
                       size_t len) {
  (void)h;
  (void)key;
  assert(len == sizeof(pending));
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
int main(void) {
  float offsets[3];
  static float samples[3][APP_OFFSET_SAMPLE_COUNT];
  for (unsigned i = 0; i < 4096; ++i)
    lut[i] = i / 1000.0f;
  assert(app_offsets_init(lut, offsets) == ESP_OK);
  assert(!app_offsets_calibration_requested() && commits == 0);
  for (unsigned p = 0; p < 3; ++p)
    assert(offsets[p] == 1.65f);

  low = true;
  assert(app_offsets_load(lut, offsets) == ESP_OK);
  assert(!app_offsets_calibration_requested() && commits == 0);
  assert(app_offsets_init(lut, offsets) == ESP_OK);
  assert(app_offsets_calibration_requested() && commits == 0);
  for (unsigned p = 0; p < 3; ++p)
    for (unsigned i = 0; i < APP_OFFSET_SAMPLE_COUNT; ++i)
      samples[p][i] = i < 100                              ? 0.1f
                      : i >= APP_OFFSET_SAMPLE_COUNT - 100 ? 3.0f
                                                           : 1.55f + p * 0.1f;
  assert(app_offsets_commit_samples(samples, 1, offsets) == ESP_OK);
  assert(fabsf(offsets[0] - 1.55f) < 0.0001f);
  assert(offsets[1] == 1.65f && offsets[2] == 1.65f);
  assert(!app_offsets_calibration_requested() && commits == 1);

  low = false;
  assert(app_offsets_init(lut, offsets) == ESP_OK);
  assert(fabsf(offsets[0] - 1.55f) < 0.0001f && commits == 1);
  assert(app_offsets_commit_samples(samples, 7, offsets) == ESP_OK);
  assert(fabsf(offsets[2] - 1.75f) < 0.0001f && commits == 2);

  stored.volts[1] = NAN;
  assert(app_offsets_init(lut, offsets) == ESP_OK && offsets[0] == 1.65f);
  stored = pending;
  stored.version = 9;
  assert(app_offsets_init(lut, offsets) == ESP_OK && offsets[2] == 1.65f);
  stored = pending;
  assert(app_offsets_load(lut, offsets) == ESP_OK);
  float previous = offsets[0];
  fail_commit = true;
  for (unsigned i = 0; i < APP_OFFSET_SAMPLE_COUNT; ++i)
    samples[0][i] = 1.6f;
  assert(app_offsets_commit_samples(samples, 1, offsets) == ESP_FAIL);
  assert(offsets[0] == previous && stored.volts[0] == previous && commits == 3);
  fail_commit = false;
  samples[0][0] = NAN;
  assert(app_offsets_commit_samples(samples, 1, offsets) ==
         ESP_ERR_INVALID_RESPONSE);
  samples[0][0] = 0;
  assert(app_offsets_commit_samples(samples, 1, offsets) ==
         ESP_ERR_INVALID_RESPONSE);
  assert(commits == 3);
  fail_init = true;
  assert(app_offsets_load(lut, offsets) == ESP_FAIL);
  assert(app_offsets_init(NULL, offsets) == ESP_ERR_INVALID_ARG);
  puts("DMA offset handoff and persistence tests passed");
}
