#include "app_control.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

static void prepare(esp_pr_model_t *model, float kp, float kr, float limit,
                    float local_tracking) {
  const esp_pr_resonator_config_t branch = {
      .frequency_hz = 60, .kr = kr, .damping_hz = 5};
  const esp_pr_config_t config = {
      .sample_rate_hz = 14400,
      .kp = kp,
      .output_min = -limit,
      .output_max = limit,
      .anti_windup_tracking_time_s = local_tracking,
      .tracking_mode = local_tracking == 0 ? ESP_PR_TRACKING_EXTERNAL
                                           : ESP_PR_TRACKING_INTERNAL,
      .resonators = &branch,
      .resonator_count = kr > 0 ? 1 : 0};
  assert(esp_pr_prepare(model, &config) == ESP_OK);
}

static void dc_plant(float target) {
  app_control_init_with_limit(2.0f);
  float current = 0, command = 0;
  // Illustrative plant only; this is not an identified hardware model.
  for (unsigned frame = 0; frame < 30 * 14400; ++frame) {
    current += (app_control_dac_to_primary_current(
                    app_control_voltage_to_dac(command)) -
                current) /
               72.0f;
    assert(app_parallel_update(&pr_fase_r, 0.3f * (target - current),
                               &command) == ESP_OK);
    assert(isfinite(command) && fabsf(command) <= 2.0f);
  }
  assert(fabsf(current - target) < 0.005f);
  assert(pr_fase_r.integral.config.kp == 0);
  assert(pr_fase_r.integral.config.kd == 0);
  assert(pr_fase_r.integral.config.output_max == 2.0f);
  assert(pr_fase_r.pr.model->output_max == 2.0f);
  float other = 1;
  assert(app_parallel_update(&pr_fase_s, 0, &other) == ESP_OK && other == 0);
}

static float mixed_plant(float kr) {
  esp_pr_model_t model;
  prepare(&model, 1, kr, 2, 0);
  app_parallel_t controller;
  assert(app_parallel_init(&controller, &model, 2, 2) == ESP_OK);
  float current = 0, command = 0, sine_sum = 0, cosine_sum = 0;
  for (unsigned frame = 0; frame < 15 * 14400; ++frame) {
    const float phase = (frame % 240) * (2.0f * 3.141592653589793f / 240);
    current += (app_control_dac_to_primary_current(
                    app_control_voltage_to_dac(command)) -
                current) /
               72.0f;
    const float target = 0.4f + 0.05f * sinf(phase);
    const float error = 0.3f * (target - current);
    assert(app_parallel_update(&controller, error, &command) == ESP_OK);
    assert(fabsf(command) <= 2);
    if (frame >= 10 * 14400) {
      sine_sum += error * sinf(phase);
      cosine_sum += error * cosf(phase);
    }
  }
  assert(fabsf(current - 0.4f) < 0.06f);
  return 1.41421356f * hypotf(sine_sum, cosine_sum) / (5 * 14400);
}

int main(void) {
  assert(fabsf(app_control_dac_to_primary_current(2048)) < 1e-6f);
  assert(fabsf(app_control_dac_to_primary_current(0) + 3.38539043f) < 1e-6f);
  dc_plant(0.9f);
  dc_plant(-0.9f);
  assert(mixed_plant(10) < 0.95f * mixed_plant(0));

  esp_pr_model_t model;
  app_parallel_t controller;
  float output;
  prepare(&model, 1.5f, 0, 2, 0);
  assert(app_parallel_init(&controller, &model, 1, 2) == ESP_OK);
  controller.integral.state.integral_term = 1.5f;
  assert(app_parallel_update(&controller, 1, &output) == ESP_OK);
  // Both requests fit individually; only their sum saturates. Correct once.
  assert(controller.requested_pr < 2 && controller.requested_integral < 2);
  assert(output == 2 && controller.saturation_error < 0);
  const float candidate_i = 1.5f + 1.0f / 14400;
  assert(fabsf(controller.integral.state.integral_term -
               (candidate_i + controller.tracking.alpha *
                                  controller.saturation_error)) < 1e-6f);
  assert(controller.pr.model->correction_alpha == 0);
  assert(controller.integral.config.anti_windup_tracking_time_s == 0);

  assert(app_parallel_reset(&controller) == ESP_OK);
  controller.integral.state.integral_term = -2;
  assert(app_parallel_update(&controller, 2, &output) == ESP_OK);
  // Opposing contributions may exceed a rail individually while their sum fits.
  assert(controller.requested_pr == 3);
  assert(output > 0.9f && output < 1.1f);
  assert(controller.saturation_error == 0);

  prepare(&model, 0, 10, 2, 0);
  assert(app_parallel_init(&controller, &model, 1, 2) == ESP_OK);
  controller.pr.branches[0].p = 2;
  controller.integral.state.integral_term = 2;
  assert(app_parallel_update(&controller, 0, &output) == ESP_OK);
  const float correction =
      controller.tracking.alpha * controller.saturation_error;
  assert(fabsf(controller.integral.state.integral_term -
               (2 + 0.5f * correction)) < 1e-6f);
  assert(fabsf(controller.pr.branches[0].p -
               (controller.pr.diagnostics.resonant_output[0] +
                0.5f * correction)) < 1e-6f);
  assert(fabsf(controller.pr.branches[0].q - model.branches[0].qp * 2) < 1e-6f);

  app_control_init_with_limit(2);
  for (unsigned i = 0; i < 10 * 14400; ++i) {
    assert(app_parallel_update(&pr_fase_r, 10, &output) == ESP_OK);
    assert(isfinite(output) && fabsf(output) <= 2);
    assert(isfinite(pr_fase_r.integral.state.integral_term));
    assert(fabsf(pr_fase_r.integral.state.integral_term) < 30);
  }
  for (unsigned i = 0; i < 14400 / 2; ++i)
    assert(app_parallel_update(&pr_fase_r, -10, &output) == ESP_OK);
  assert(output < 0);
  assert(app_parallel_update(&pr_fase_r, NAN, &output) != ESP_OK);
  assert(output == 0 && pr_fase_r.integral.state.integral_term == 0);
  assert(pr_fase_r.saturation_error == 0);
  assert(app_parallel_update(&pr_fase_r, 0, &output) == ESP_OK && output == 0);

  prepare(&model, 1, 10, 6, 0);
  assert(app_parallel_init(&controller, &model, 0, 6) == ESP_OK);
  assert(!controller.integral_enabled && controller.pr_tracking_share == 1);
  assert(app_parallel_update(&controller, 100, &output) == ESP_OK);
  assert(app_control_voltage_to_dac(output) == DAC_MAX_CODE);
  assert(controller.integral.state.integral_term == 0);
  assert(app_parallel_init(&controller, &model, 2, 2) != ESP_OK);
  prepare(&model, 1, 10, 2, 0.01f);
  assert(app_parallel_init(&controller, &model, 2, 2) != ESP_OK);
  puts("coordinated PR+I anti-windup tests passed");
}
