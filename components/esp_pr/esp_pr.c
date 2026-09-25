#include "esp_pr.h"

#include <float.h>
#include <math.h>

/* Internal validation shared by harmonic construction and model preparation.
 * The Nyquist constraint depends on sample rate and is checked by prepare. */
static bool branch_valid(const esp_pr_resonator_config_t *b) {
  return isfinite(b->frequency_hz) && b->frequency_hz > 0.0f &&
         isfinite(b->kr) && b->kr >= 0.0f && isfinite(b->damping_hz) &&
         b->damping_hz > 0.0f;
}

esp_err_t esp_pr_make_harmonics(esp_pr_resonator_config_t *destination,
                                size_t count, float fundamental_hz,
                                const unsigned *orders, const float *kr,
                                const float *damping_hz) {
  /* This external convenience routine validates all entries into temporary
   * fixed storage so a failure cannot partially overwrite destination. */
  if (!destination || !orders || !kr || !damping_hz || count == 0 ||
      count > ESP_PR_MAX_RESONATORS || !isfinite(fundamental_hz) ||
      fundamental_hz <= 0.0f) {
    return ESP_ERR_INVALID_ARG;
  }
  esp_pr_resonator_config_t temporary[ESP_PR_MAX_RESONATORS];
  for (size_t i = 0; i < count; ++i) {
    temporary[i] =
        (esp_pr_resonator_config_t){.frequency_hz = fundamental_hz * orders[i],
                                    .kr = kr[i],
                                    .damping_hz = damping_hz[i]};
    if (!branch_valid(&temporary[i]))
      return ESP_ERR_INVALID_ARG;
  }
  for (size_t i = 0; i < count; ++i)
    destination[i] = temporary[i];
  return ESP_OK;
}

esp_err_t esp_pr_prepare(esp_pr_model_t *model, const esp_pr_config_t *config) {
  /* Validate top-level relationships before computing coefficients. The local
   * result provides failure atomicity for the caller-owned destination
   * model. */
  if (!model || !config || !isfinite(config->sample_rate_hz) ||
      config->sample_rate_hz <= 0.0f || !isfinite(config->kp) ||
      !isfinite(config->output_min) || !isfinite(config->output_max) ||
      config->output_min >= config->output_max ||
      !isfinite(config->anti_windup_tracking_time_s) ||
      config->anti_windup_tracking_time_s < 0.0f ||
      config->resonator_count > ESP_PR_MAX_RESONATORS ||
      (config->resonator_count && !config->resonators)) {
    return ESP_ERR_INVALID_ARG;
  }
  esp_pr_model_t result = {0};
  double gain_sum = 0.0;
  /* The gain sum defines fixed anti-windup distribution weights. A zero-gain
   * branch remains present but contributes and receives no correction. */
  for (size_t i = 0; i < config->resonator_count; ++i) {
    const esp_pr_resonator_config_t *b = &config->resonators[i];
    if (!branch_valid(b) || b->frequency_hz >= config->sample_rate_hz * 0.5)
      return ESP_ERR_INVALID_ARG;
    gain_sum += b->kr;
  }

  const double pi = 3.14159265358979323846;
  for (size_t i = 0; i < config->resonator_count; ++i) {
    const esp_pr_resonator_config_t *b = &config->resonators[i];
    /* Trapezoidal integration with frequency prewarping: r = w/k,
     * k = w/tan(w*Ts/2). Continuous realization:
     * q' = w*p; p' = -w*q - 2*wc*p + 2*wc*Kr*e; output = p.
     * Both input endpoints are used, hence e[k] + e[k-1]. */
    const double r = tan(pi * b->frequency_hz / config->sample_rate_hz);
    const double v = 2.0 * b->damping_hz / b->frequency_hz * r;
    const double d = 1.0 + v + r * r;
    const double pe = b->kr * v / d;
    const double qe = r * pe;
    if (!isfinite(qe) || qe > FLT_MAX || !isfinite(pe) || pe > FLT_MAX)
      return ESP_ERR_INVALID_ARG;
    esp_pr_coefficients_t *c = &result.branches[i];
    *c = (esp_pr_coefficients_t){
        .qq = (float)((1.0 + v - r * r) / d),
        .qp = (float)(2.0 * r / d),
        .pq = (float)(-2.0 * r / d),
        .pp = (float)((1.0 - v - r * r) / d),
        .qe = (float)qe,
        .pe = (float)pe,
        .correction_share = gain_sum > 0.0 ? (float)(b->kr / gain_sum) : 0.0f};
    /* Reject poles on/outside the unit circle after float rounding.
     * These are the strict second-order Jury conditions. */
    const double trace = (double)c->qq + c->pp;
    const double determinant = (double)c->qq * c->pp - (double)c->qp * c->pq;
    if (!(1.0 - determinant > 0.0 && 1.0 - trace + determinant > 0.0 &&
          1.0 + trace + determinant > 0.0))
      return ESP_ERR_INVALID_ARG;
  }
  result.resonator_count = config->resonator_count;
  result.kp = config->kp;
  result.output_min = config->output_min;
  result.output_max = config->output_max;
  /* Compute the exact per-sample relaxation fraction once. -expm1(-x) retains
   * accuracy when Ts/Tt is small; update then needs only multiplication. */
  result.correction_alpha =
      config->anti_windup_tracking_time_s > 0.0f
          ? (float)-expm1(-1.0 / ((double)config->sample_rate_hz *
                                  config->anti_windup_tracking_time_s))
          : 0.0f;
  result.prepared = true;
  *model = result;
  return ESP_OK;
}

esp_err_t esp_pr_init(esp_pr_t *controller, const esp_pr_model_t *model) {
  /* External initialization binds the borrowed model and zeroes all state in
   * one assignment. No configuration arrays are referenced after
   * preparation. */
  if (!controller || !model || !model->prepared)
    return ESP_ERR_INVALID_ARG;
  *controller = (esp_pr_t){.model = model};
  return ESP_OK;
}

esp_err_t ESP_PR_IRAM esp_pr_reset(esp_pr_t *controller) {
  /* External lifecycle operation. The fixed bound makes execution finite, but
   * callers must serialize it with updates and diagnostic reads. */
  if (!controller || !controller->model || !controller->model->prepared)
    return ESP_ERR_INVALID_STATE;
  controller->previous_error = 0.0f;
  for (size_t i = 0; i < ESP_PR_MAX_RESONATORS; ++i) {
    controller->branches[i].q = 0.0f;
    controller->branches[i].p = 0.0f;
    controller->diagnostics.resonant_output[i] = 0.0f;
  }
  controller->diagnostics.error = 0.0f;
  controller->diagnostics.proportional = 0.0f;
  controller->diagnostics.unsaturated_output = 0.0f;
  controller->diagnostics.output = 0.0f;
  controller->diagnostics.saturation_error = 0.0f;
  controller->diagnostics.saturated = false;
  return ESP_OK;
}

esp_err_t ESP_PR_IRAM esp_pr_update(esp_pr_t *controller, float reference,
                                    float measurement, float *output) {
  /* Validate the instance and external inputs before computing next state. */
  if (!controller || !controller->model || !controller->model->prepared)
    return ESP_ERR_INVALID_STATE;
  if (!output || !isfinite(reference) || !isfinite(measurement))
    return ESP_ERR_INVALID_ARG;
  const esp_pr_model_t *model = controller->model;
  const float error = reference - measurement;
  const float input = error + controller->previous_error;
  const float proportional = model->kp * error;
  if (!isfinite(error) || !isfinite(input) || !isfinite(proportional))
    return ESP_ERR_INVALID_ARG;
  float requested = proportional;
  esp_pr_resonator_state_t next[ESP_PR_MAX_RESONATORS];
  float contributions[ESP_PR_MAX_RESONATORS];
  /* Linear trapezoidal state update. Results remain temporary until every
   * branch and the subsequent correction have passed finite-value checks. */
  for (size_t i = 0; i < model->resonator_count; ++i) {
    const esp_pr_coefficients_t *c = &model->branches[i];
    const esp_pr_resonator_state_t *x = &controller->branches[i];
    next[i].q = c->qq * x->q + c->qp * x->p + c->qe * input;
    next[i].p = c->pq * x->q + c->pp * x->p + c->pe * input;
    contributions[i] = next[i].p;
    requested += next[i].p;
    if (!isfinite(next[i].q) || !isfinite(next[i].p) || !isfinite(requested))
      return ESP_ERR_INVALID_ARG;
  }
  const float applied = requested > model->output_max   ? model->output_max
                        : requested < model->output_min ? model->output_min
                                                        : requested;
  const float delta = applied - requested;
  if (!isfinite(delta))
    return ESP_ERR_INVALID_ARG;
  /* Post-sample tracking substep. q is unchanged; correction of p preserves
   * oscillator evolution on the following step. Shares sum to one for active
   * branches; zero-gain branches receive no correction. alpha in [0,1] is the
   * exact relaxation factor for this correction substep, not a stability
   * guarantee for the complete sampled nonlinear controller/plant. */
  const float correction = model->correction_alpha * delta;
  for (size_t i = 0; i < model->resonator_count; ++i) {
    next[i].p += model->branches[i].correction_share * correction;
    if (!isfinite(next[i].p))
      return ESP_ERR_INVALID_ARG;
  }
  /* Commit only after every calculation succeeds. */
  for (size_t i = 0; i < model->resonator_count; ++i) {
    controller->branches[i] = next[i];
    controller->diagnostics.resonant_output[i] = contributions[i];
  }
  controller->previous_error = error;
  controller->diagnostics.error = error;
  controller->diagnostics.proportional = proportional;
  controller->diagnostics.unsaturated_output = requested;
  controller->diagnostics.output = applied;
  controller->diagnostics.saturation_error = delta;
  controller->diagnostics.saturated = requested != applied;
  *output = applied;
  return ESP_OK;
}

esp_err_t esp_pr_get_diagnostics(const esp_pr_t *controller,
                                 esp_pr_diagnostics_t *diagnostics) {
  /* External snapshot copy. Synchronization is intentionally owned by the
   * application because the component creates no tasks or locks. */
  if (!controller || !controller->model || !controller->model->prepared)
    return ESP_ERR_INVALID_STATE;
  if (!diagnostics)
    return ESP_ERR_INVALID_ARG;
  *diagnostics = controller->diagnostics;
  return ESP_OK;
}
