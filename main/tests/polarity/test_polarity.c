#include "app_polarity.h"
#include "fakes.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

enum { NEGATIVE, POSITIVE, WEAK, NOISY, DRIFT, FLIP, RAIL, ADC_FAIL, SPI_FAIL };
static int scenario;
static unsigned samples, pulses, writes, deleted;
static bool high;
static char logs[12000];
static float lut[4096];

void test_log(const char *format, ...) {
  size_t length = strlen(logs);
  va_list args;
  va_start(args, format);
  vsnprintf(logs + length, sizeof(logs) - length, format, args);
  va_end(args);
}
const char *esp_err_to_name(esp_err_t err) {
  (void)err;
  return "injected-error";
}
void vTaskDelay(unsigned ticks) { assert(ticks > 0); }
unsigned xTaskGetTickCount(void) { return 0; }
int xTaskDelayUntil(unsigned *release, unsigned ticks) {
  *release += ticks;
  return 1;
}
esp_err_t mcp4922_write_channels(mcp4922_context_t *dac,
                                 const uint16_t *values) {
  (void)dac;
  ++writes;
  assert(values[1] == 2048 && values[2] == 2048 && values[3] == 2048);
  assert(values[0] == 2048 || values[0] == 2080);
  if (values[0] == 2080) {
    ++pulses;
    if (scenario == SPI_FAIL)
      return ESP_FAIL;
  }
  high = values[0] == 2080;
  return ESP_OK;
}
esp_err_t adc_oneshot_new_unit(const adc_oneshot_unit_init_cfg_t *cfg,
                               adc_oneshot_unit_handle_t *adc) {
  assert(cfg->unit_id == ADC_UNIT_1);
  *adc = (void *)1;
  return ESP_OK;
}
esp_err_t adc_oneshot_config_channel(adc_oneshot_unit_handle_t adc,
                                     adc_channel_t ch,
                                     const adc_oneshot_chan_cfg_t *cfg) {
  (void)adc;
  assert(ch == 3 && cfg->atten == 12 && cfg->bitwidth == 12);
  return ESP_OK;
}
esp_err_t adc_oneshot_read(adc_oneshot_unit_handle_t adc, adc_channel_t ch,
                           int *raw) {
  (void)adc;
  assert(ch == 3);
  ++samples;
  if (scenario == ADC_FAIL && high)
    return ESP_FAIL;
  if (scenario == RAIL && high) {
    *raw = 4095;
    return ESP_OK;
  }
  int delta =
      scenario == POSITIVE || (scenario == FLIP && pulses > 1) ? 20 : -20;
  *raw = 1650 + (high && scenario != WEAK ? delta : 0);
  if (scenario == NOISY)
    *raw += samples % 2 ? 40 : -40;
  if (scenario == DRIFT && pulses && !high)
    *raw += 30;
  return ESP_OK;
}
esp_err_t adc_oneshot_del_unit(adc_oneshot_unit_handle_t adc) {
  (void)adc;
  ++deleted;
  return ESP_OK;
}

static void run(int mode, const char *expected) {
  scenario = mode;
  samples = pulses = writes = deleted = 0;
  high = false;
  logs[0] = 0;
  mcp4922_context_t dac = {0};
  app_polarity_run(&dac, lut, 32);
  assert(strstr(logs, expected) != NULL);
  assert(deleted == 1 && !high);
}
int main(void) {
  for (unsigned i = 0; i < 4096; ++i)
    lut[i] = i / 1000.0f;
  run(NEGATIVE, "COMPATIBLE:");
  assert(pulses == 5 && writes == 11);
  run(POSITIVE, "INVERTED:");
  assert(pulses == 5 && writes == 11);
  run(WEAK, "INCONCLUSIVE:");
  assert(pulses == 1);
  run(NOISY, "INCONCLUSIVE:");
  run(DRIFT, "INCONCLUSIVE:");
  run(FLIP, "INCONCLUSIVE:");
  assert(pulses == 2);
  run(RAIL, "INCONCLUSIVE:");
  assert(writes == 3);
  run(ADC_FAIL, "INCONCLUSIVE:");
  assert(writes == 3);
  run(SPI_FAIL, "DAC fault:");
  assert(writes == 2); // no retry of latched driver
  puts("automatic polarity tests passed");
}
