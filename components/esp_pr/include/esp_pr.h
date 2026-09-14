#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef ESP_PLATFORM
#include "esp_attr.h"
#include "esp_err.h"
#define ESP_PR_IRAM IRAM_ATTR
#else
#define ESP_PR_IRAM
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Fixed storage: no allocator is used, including during initialization. */
#define ESP_PR_MAX_RESONATORS 16U

typedef struct {
  float frequency_hz;
  float kr;         /* Nonnegative peak gain of this resonant branch. */
  float damping_hz; /* wc/(2*pi), not the full -3 dB bandwidth. Must be > 0. */
} esp_pr_resonator_config_t;

typedef struct {
  float sample_rate_hz;
  float kp;
  float output_min;
  float output_max;
  float anti_windup_tracking_time_s; /* 0 disables correction; otherwise > 0. */
  const esp_pr_resonator_config_t *resonators;
  size_t resonator_count;
} esp_pr_config_t;

/* Prepared coefficients are public for static storage, but read-only after
 * prepare. A model may be shared by independent controllers. Do not modify or
 * prepare it again while controllers refer to it. Place it in internal RAM
 * when update must run with flash cache disabled. */
typedef struct {
  float qq, qp, pq, pp, qe, pe;
  float correction_share;
} esp_pr_coefficients_t;

typedef struct {
  esp_pr_coefficients_t branches[ESP_PR_MAX_RESONATORS];
  size_t resonator_count;
  float kp, output_min, output_max, correction_alpha;
  bool prepared;
} esp_pr_model_t;

typedef struct {
  float q, p; /* Quadrature and in-phase resonant states, in output units. */
} esp_pr_resonator_state_t;

typedef struct {
  float error, proportional, unsaturated_output, output, saturation_error;
  float resonant_output[ESP_PR_MAX_RESONATORS]; /* Before state correction. */
  bool saturated;
} esp_pr_diagnostics_t;

typedef struct {
  const esp_pr_model_t *model;
  esp_pr_resonator_state_t branches[ESP_PR_MAX_RESONATORS];
  float previous_error;
  esp_pr_diagnostics_t diagnostics;
} esp_pr_t;

/* Initialization functions are not real-time functions. Failure leaves the
 * destination unchanged. Configuration arrays are copied into coefficients. */
esp_err_t esp_pr_prepare(esp_pr_model_t *model, const esp_pr_config_t *config);
esp_err_t esp_pr_init(esp_pr_t *controller, const esp_pr_model_t *model);

/* Build explicit frequency configurations from harmonic orders. kr and damping
 * contain count entries. Validation of Nyquist limits happens in prepare. */
esp_err_t esp_pr_make_harmonics(esp_pr_resonator_config_t *destination,
                                size_t count, float fundamental_hz,
                                const unsigned *orders, const float *kr,
                                const float *damping_hz);

esp_err_t esp_pr_reset(esp_pr_t *controller);

/* Call exactly once per configured sample period. No allocation, division,
 * transcendental functions or logging. Invalid/nonfinite input or arithmetic
 * overflow leaves controller and *output unchanged. Caller defines fallback.
 * One owner per controller; diagnostic snapshots require synchronization if
 * read from another task/core. output must not alias controller/model storage.
 * Limits describe the actuator command AFTER all summed controller terms. */
esp_err_t esp_pr_update(esp_pr_t *controller, float reference,
                        float measurement, float *output);
esp_err_t esp_pr_get_diagnostics(const esp_pr_t *controller,
                                 esp_pr_diagnostics_t *diagnostics);

#ifdef __cplusplus
}
#endif
