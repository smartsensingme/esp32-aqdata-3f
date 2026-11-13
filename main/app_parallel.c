#include "app_parallel.h"
#include "esp_attr.h"
#include "sdkconfig.h"
#include <math.h>

esp_err_t app_parallel_init(app_parallel_t *controller,
                            const esp_pr_model_t *model, float ki,
                            float limit) {
  if (!controller || !model || !model->prepared || !isfinite(limit) ||
      limit <= 0.0f || !isfinite(ki) || ki < 0.0f ||
      model->output_max != limit || model->output_min != -limit ||
      model->tracking_mode != ESP_PR_TRACKING_EXTERNAL)
    return ESP_ERR_INVALID_ARG;

  *controller = (app_parallel_t){0};
  controller->integral_enabled = ki > 0.0f;
  const esp_pid_config_t config = {
      .kp = 0.0f,
      .ki = ki,
      .kd = 0.0f,
      .output_min = -limit,
      .output_max = limit,
      .derivative_filter_tau_s = 0.0f,
      .anti_windup_tracking_time_s = 0.0f,
      .maximum_dt_s = 2.0f / 14400.0f,
      .derivative_source = ESP_PID_DERIVATIVE_ON_MEASUREMENT,
      .tracking_mode = ESP_PID_TRACKING_EXTERNAL,
  };
  esp_err_t result = esp_pr_init(&controller->pr, model);
  if (result == ESP_OK)
    result = esp_pid_init(&controller->integral, &config);
  if (result != ESP_OK)
    return result;

  const bool resonant = model->tracking_supported;
  const esp_anti_windup_participant_t participants[] = {
      {.kind = ESP_ANTI_WINDUP_PR,
       .controller.pr = &controller->pr,
       .weight = resonant ? 1.0f : 0.0f},
      {.kind = ESP_ANTI_WINDUP_PID,
       .controller.pid = &controller->integral,
       .weight = 1.0f},
  };
  const esp_anti_windup_config_t tracking = {
      .minimum = -limit,
      .maximum = limit,
      .dt_s = 1.0f / 14400.0f,
      .tracking_time_s = CONFIG_APP_PR_TRACKING_TIME_US / 1000000.0f,
      .participants = participants,
      .count = controller->integral_enabled ? 2 : 1,
  };
  result = esp_anti_windup_init(&controller->tracking, &tracking);
  if (result != ESP_OK)
    return result;
  controller->pr_tracking_share = controller->tracking.participants[0].weight;
  controller->integral_tracking_share =
      controller->integral_enabled ? controller->tracking.participants[1].weight
                                   : 0.0f;
  controller->limit = limit;
  return ESP_OK;
}

esp_err_t IRAM_ATTR app_parallel_reset(app_parallel_t *controller) {
  if (!controller)
    return ESP_ERR_INVALID_ARG;
  const esp_err_t result = esp_anti_windup_reset(&controller->tracking);
  controller->requested_pr = controller->requested_integral = 0.0f;
  controller->requested = controller->applied = controller->saturation_error =
      0.0f;
  return result;
}

esp_err_t IRAM_ATTR app_parallel_update(app_parallel_t *controller, float error,
                                        float *output) {
  if (!controller || !output)
    return ESP_ERR_INVALID_ARG;
  *output = 0.0f;
  esp_anti_windup_result_t result;
  const esp_err_t err =
      esp_anti_windup_update(&controller->tracking, error, 0.0f, &result);
  // Retain application telemetry names; all control coordination is in
  // component.
  controller->requested_pr = result.requests[0];
  controller->requested_integral = result.requests[1];
  controller->requested = result.requested;
  controller->applied = result.applied;
  controller->saturation_error = result.saturation_error;
  if (err == ESP_OK)
    *output = result.applied;
  return err;
}
