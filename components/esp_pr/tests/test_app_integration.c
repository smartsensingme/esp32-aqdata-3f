#include "app_control.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

int main(void) {
  app_control_init();
  assert(pr_fase_r.model == pr_fase_s.model &&
         pr_fase_s.model == pr_fase_t.model);
  assert(app_control_voltage_to_dac(-6) == 0);
  assert(app_control_voltage_to_dac(0) == 2048);
  assert(app_control_voltage_to_dac(6) == 4095);
  assert(app_control_voltage_to_dac(-100) == 0);
  assert(app_control_voltage_to_dac(100) == 4095);
  assert(app_control_voltage_to_dac(NAN) == 2048);
  assert(app_control_voltage_to_dac(INFINITY) == 2048);
  for (unsigned code = 0; code <= DAC_MAX_CODE; ++code) {
    float volts = -6.0f + 12.0f * code / DAC_MAX_CODE;
    assert(app_control_voltage_to_dac(volts) == code);
  }
  assert(app_control_process_phase(&pr_fase_r, .1f) == 6);
  assert(app_control_process_phase(&pr_fase_s, -.1f) == -6);
  assert(app_control_process_phase(&pr_fase_t, 0) == 0);
  assert(app_control_process_phase(&pr_fase_r, NAN) == 0);
  assert(app_control_process_phase(&pr_fase_r, 0) == 0);
  assert(pr_fase_s.previous_error == -.1f);
  puts("PASS application integration, independent phases, fault recovery and "
       "all DAC codes");
  return 0;
}
