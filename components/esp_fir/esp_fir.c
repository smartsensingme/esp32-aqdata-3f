#include "esp_fir.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

static bool valid_structure(esp_fir_structure_t structure) {
  return structure >= ESP_FIR_GENERAL && structure <= ESP_FIR_ANTISYMMETRIC;
}

static size_t stored_coefficient_count(const esp_fir_config_t *config) {
  if (config->coefficient_layout == ESP_FIR_COEFFICIENTS_FULL)
    return config->tap_count;
  if (config->structure == ESP_FIR_SYMMETRIC)
    return config->tap_count / 2U + config->tap_count % 2U;
  if (config->structure == ESP_FIR_ANTISYMMETRIC)
    return config->tap_count / 2U;
  return 0;
}

size_t esp_fir_history_size(size_t tap_count) {
  return tap_count <= SIZE_MAX / (2U * sizeof(float)) ? 2U * tap_count : 0U;
}

esp_err_t esp_fir_prepare(esp_fir_model_t *model,
                          const esp_fir_config_t *config) {
  if (!model || !config || !config->coefficients || config->tap_count == 0U ||
      !valid_structure(config->structure) ||
      (config->coefficient_layout != ESP_FIR_COEFFICIENTS_FULL &&
       config->coefficient_layout != ESP_FIR_COEFFICIENTS_FOLDED) ||
      (config->startup != ESP_FIR_STARTUP_ZERO &&
       config->startup != ESP_FIR_STARTUP_PRIME_FIRST_SAMPLE) ||
      (config->structure == ESP_FIR_GENERAL &&
       config->coefficient_layout == ESP_FIR_COEFFICIENTS_FOLDED) ||
      !isfinite(config->symmetry_tolerance) ||
      config->symmetry_tolerance < 0.0f ||
      esp_fir_history_size(config->tap_count) == 0U ||
      config->coefficient_count != stored_coefficient_count(config)) {
    return ESP_ERR_INVALID_ARG;
  }

  const size_t pairs = config->tap_count / 2U;
  double dc_gain = 0.0;
  for (size_t i = 0; i < config->coefficient_count; ++i) {
    if (!isfinite(config->coefficients[i]))
      return ESP_ERR_INVALID_ARG;
  }

  if (config->coefficient_layout == ESP_FIR_COEFFICIENTS_FULL) {
    for (size_t i = 0; i < config->tap_count; ++i)
      dc_gain += config->coefficients[i];
    if (config->structure != ESP_FIR_GENERAL) {
      const float sign = config->structure == ESP_FIR_SYMMETRIC ? 1.0f : -1.0f;
      for (size_t i = 0; i < pairs; ++i) {
        const float expected = sign * config->coefficients[i];
        const float actual = config->coefficients[config->tap_count - 1U - i];
        if (fabsf(actual - expected) > config->symmetry_tolerance)
          return ESP_ERR_INVALID_ARG;
      }
      if (config->structure == ESP_FIR_ANTISYMMETRIC &&
          (config->tap_count & 1U) &&
          fabsf(config->coefficients[pairs]) > config->symmetry_tolerance) {
        return ESP_ERR_INVALID_ARG;
      }
    }
  } else if (config->structure == ESP_FIR_SYMMETRIC) {
    for (size_t i = 0; i < pairs; ++i)
      dc_gain += 2.0 * config->coefficients[i];
    if (config->tap_count & 1U)
      dc_gain += config->coefficients[pairs];
  } else {
    dc_gain = 0.0;
  }

  const esp_fir_model_t result = {
      .coefficients = config->coefficients,
      .tap_count = config->tap_count,
      .pair_count = pairs,
      .dc_gain = (float)dc_gain,
      .structure = config->structure,
      .coefficient_layout = config->coefficient_layout,
      .startup = config->startup,
      .prepared = true,
  };
  *model = result;
  return ESP_OK;
}

esp_err_t esp_fir_init(esp_fir_t *filter, const esp_fir_model_t *model,
                       float *history, size_t history_length) {
  if (!filter || !model || !model->prepared)
    return ESP_ERR_INVALID_STATE;
  if (!history)
    return ESP_ERR_INVALID_ARG;
  const size_t required = esp_fir_history_size(model->tap_count);
  if (required == 0U || history_length < required)
    return ESP_ERR_INVALID_SIZE;
  memset(history, 0, required * sizeof(*history));
  *filter = (esp_fir_t){.model = model,
                        .history = history,
                        .head = 0,
                        .initialized = model->startup == ESP_FIR_STARTUP_ZERO};
  return ESP_OK;
}

esp_err_t esp_fir_reset(esp_fir_t *filter) {
  if (!filter || !filter->model || !filter->model->prepared || !filter->history)
    return ESP_ERR_INVALID_STATE;
  const size_t count = esp_fir_history_size(filter->model->tap_count);
  memset(filter->history, 0, count * sizeof(*filter->history));
  filter->head = 0;
  filter->initialized = filter->model->startup == ESP_FIR_STARTUP_ZERO;
  return ESP_OK;
}

static inline const float *ESP_FIR_IRAM insert_sample(esp_fir_t *filter,
                                                      float sample) {
  const size_t taps = filter->model->tap_count;
  if (!filter->initialized) {
    for (size_t i = 0; i < 2U * taps; ++i)
      filter->history[i] = sample;
    filter->head = 0;
    filter->initialized = true;
    return filter->history;
  }
  filter->head = filter->head == 0U ? taps - 1U : filter->head - 1U;
  filter->history[filter->head] = sample;
  filter->history[filter->head + taps] = sample;
  return &filter->history[filter->head];
}

float ESP_FIR_IRAM esp_fir_process_general(esp_fir_t *filter, float sample) {
  const esp_fir_model_t *model = filter->model;
  if (!filter->initialized &&
      model->startup == ESP_FIR_STARTUP_PRIME_FIRST_SAMPLE) {
    (void)insert_sample(filter, sample);
    return sample * model->dc_gain;
  }
  const float *x = insert_sample(filter, sample);
  const float *h = model->coefficients;
  float y = 0.0f;
  for (size_t i = 0; i < model->tap_count; ++i)
    y += h[i] * x[i];
  return y;
}

float ESP_FIR_IRAM esp_fir_process_symmetric(esp_fir_t *filter, float sample) {
  const esp_fir_model_t *model = filter->model;
  if (!filter->initialized &&
      model->startup == ESP_FIR_STARTUP_PRIME_FIRST_SAMPLE) {
    (void)insert_sample(filter, sample);
    return sample * model->dc_gain;
  }
  const float *x = insert_sample(filter, sample);
  const float *h = model->coefficients;
  const size_t pairs = model->pair_count;
  const size_t last = model->tap_count - 1U;
  float y = 0.0f;
  size_t i = 0;
  for (; i + 3U < pairs; i += 4U) {
    y += h[i] * (x[i] + x[last - i]);
    y += h[i + 1U] * (x[i + 1U] + x[last - i - 1U]);
    y += h[i + 2U] * (x[i + 2U] + x[last - i - 2U]);
    y += h[i + 3U] * (x[i + 3U] + x[last - i - 3U]);
  }
  for (; i < pairs; ++i)
    y += h[i] * (x[i] + x[last - i]);
  if (model->tap_count & 1U)
    y += h[pairs] * x[pairs];
  return y;
}

float ESP_FIR_IRAM esp_fir_process_antisymmetric(esp_fir_t *filter,
                                                 float sample) {
  const esp_fir_model_t *model = filter->model;
  if (!filter->initialized &&
      model->startup == ESP_FIR_STARTUP_PRIME_FIRST_SAMPLE) {
    (void)insert_sample(filter, sample);
    return sample * model->dc_gain;
  }
  const float *x = insert_sample(filter, sample);
  const float *h = model->coefficients;
  const size_t pairs = model->pair_count;
  const size_t last = model->tap_count - 1U;
  float y = 0.0f;
  size_t i = 0;
  for (; i + 3U < pairs; i += 4U) {
    y += h[i] * (x[i] - x[last - i]);
    y += h[i + 1U] * (x[i + 1U] - x[last - i - 1U]);
    y += h[i + 2U] * (x[i + 2U] - x[last - i - 2U]);
    y += h[i + 3U] * (x[i + 3U] - x[last - i - 3U]);
  }
  for (; i < pairs; ++i)
    y += h[i] * (x[i] - x[last - i]);
  return y;
}

float ESP_FIR_IRAM esp_fir_process(esp_fir_t *filter, float sample) {
  switch (filter->model->structure) {
  case ESP_FIR_SYMMETRIC:
    return esp_fir_process_symmetric(filter, sample);
  case ESP_FIR_ANTISYMMETRIC:
    return esp_fir_process_antisymmetric(filter, sample);
  default:
    return esp_fir_process_general(filter, sample);
  }
}
