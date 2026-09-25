#pragma once

/**
 * @file esp_pr.h
 * @brief Allocation-free proportional multi-resonant controller.
 *
 * A prepared immutable model contains tuning and discrete coefficients. One or
 * more controller instances may share that model while keeping independent
 * resonator histories and diagnostics. Preparation is a task-context operation;
 * update and reset are IRAM hot-path functions on ESP-IDF targets.
 */

#include <stdbool.h>
#include <stddef.h>

#ifdef ESP_PLATFORM
#include "esp_attr.h"
#include "esp_err.h"
/** Places controller update/reset code in IRAM in ESP-IDF builds. */
#define ESP_PR_IRAM IRAM_ATTR
#else
/** Empty host-test replacement for the ESP-IDF IRAM attribute. */
#define ESP_PR_IRAM
/** Host-test replacement for ESP-IDF's error-code type. */
typedef int esp_err_t;
/** Host-test success code. */
#define ESP_OK 0
/** Host-test invalid-argument code. */
#define ESP_ERR_INVALID_ARG 0x102
/** Host-test invalid-state code. */
#define ESP_ERR_INVALID_STATE 0x103
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** Maximum number of parallel resonant branches reserved in model and state. */
#define ESP_PR_MAX_RESONATORS 16U

/** @brief Continuous-design parameters for one resonant branch. */
typedef struct {
  /** Center frequency in hertz; finite, positive, and below Nyquist. */
  float frequency_hz;
  /** Nonnegative center-frequency gain; zero disables the branch. */
  float kr;
  /**
   * Damping frequency wc/(2*pi), in hertz; finite and greater than zero. The
   * corresponding continuous branch has full -3 dB bandwidth 2*damping_hz.
   */
  float damping_hz;
} esp_pr_resonator_config_t;

/**
 * @brief Complete controller configuration consumed by esp_pr_prepare().
 *
 * The structure and resonator array may leave scope after successful
 * preparation because all required values are converted and copied.
 */
typedef struct {
  /** Update rate in samples per second; finite and greater than zero. */
  float sample_rate_hz;
  /** Common proportional gain applied to reference minus measurement. */
  float kp;
  /** Minimum command after summing proportional and resonant terms. */
  float output_min;
  /** Maximum command; finite and strictly greater than output_min. */
  float output_max;
  /**
   * Anti-windup tracking time Tt in seconds. Zero disables state correction;
   * otherwise it must be finite and positive. This is the only anti-windup
   * tuning value and is shared by all active branches.
   */
  float anti_windup_tracking_time_s;
  /** Branch array; may be null only when resonator_count is zero. */
  const esp_pr_resonator_config_t *resonators;
  /** Number of configured branches, from zero through 16. */
  size_t resonator_count;
} esp_pr_config_t;

/**
 * @brief Prepared float coefficients for one discrete resonator.
 *
 * These are implementation-facing public fields provided for static storage.
 * Configure branches through esp_pr_resonator_config_t and treat these values
 * as immutable.
 */
typedef struct {
  /** q[k] coefficient multiplying q[k-1]. */
  float qq;
  /** q[k] coefficient multiplying p[k-1]. */
  float qp;
  /** p[k] coefficient multiplying q[k-1]. */
  float pq;
  /** p[k] coefficient multiplying p[k-1]. */
  float pp;
  /** q[k] coefficient multiplying e[k]+e[k-1]. */
  float qe;
  /** p[k] coefficient multiplying e[k]+e[k-1]. */
  float pe;
  /** Fraction Kr_i/sum(Kr) of the shared anti-windup correction. */
  float correction_share;
} esp_pr_coefficients_t;

/**
 * @brief Immutable prepared controller model shared by instances.
 *
 * Fields are public for caller-controlled static allocation. Do not modify or
 * prepare a model while any controller references it. Place it in internal RAM
 * when cache-independent update execution is required.
 */
typedef struct {
  /** Prepared coefficients; the first resonator_count entries are active. */
  esp_pr_coefficients_t branches[ESP_PR_MAX_RESONATORS];
  /** Number of active prepared branches, from zero through 16. */
  size_t resonator_count;
  /** Common proportional gain copied from the configuration. */
  float kp;
  /** Applied-output lower saturation limit. */
  float output_min;
  /** Applied-output upper saturation limit. */
  float output_max;
  /** Per-sample correction fraction 1-exp(-Ts/Tt), or zero when disabled. */
  float correction_alpha;
  /** True only after successful esp_pr_prepare(). */
  bool prepared;
} esp_pr_model_t;

/** @brief Mutable quadrature and in-phase state of one resonant branch. */
typedef struct {
  /** Quadrature state q in controller-output units. */
  float q;
  /** In-phase state p and branch output, in controller-output units. */
  float p;
} esp_pr_resonator_state_t;

/**
 * @brief Snapshot of the most recently committed successful update.
 *
 * Resonant contributions are captured before the post-sample anti-windup state
 * correction. They sum with proportional to form unsaturated_output.
 */
typedef struct {
  /** Current error reference-measurement, in input units. */
  float error;
  /** Current Kp*error contribution, in output units. */
  float proportional;
  /** Requested command before saturation, in output units. */
  float unsaturated_output;
  /** Applied command after saturation, in output units. */
  float output;
  /** output-unsaturated_output; zero when not saturated. */
  float saturation_error;
  /** Pre-correction p output for each branch; inactive entries are zero. */
  float resonant_output[ESP_PR_MAX_RESONATORS];
  /** True when the requested output was outside the configured limits. */
  bool saturated;
} esp_pr_diagnostics_t;

/**
 * @brief Mutable state and diagnostics for one controller signal stream.
 *
 * Use one serialized owner per instance. Multiple instances may share model,
 * but their states must remain independent. A model must outlive its instances.
 */
typedef struct {
  /** Borrowed pointer to the immutable prepared model. */
  const esp_pr_model_t *model;
  /** Dynamic branch states; only model->resonator_count entries are active. */
  esp_pr_resonator_state_t branches[ESP_PR_MAX_RESONATORS];
  /** Previous successful update error, used by trapezoidal integration. */
  float previous_error;
  /** Diagnostics from the most recently committed successful update. */
  esp_pr_diagnostics_t diagnostics;
} esp_pr_t;

/**
 * @brief Prepare immutable discrete coefficients from continuous tuning.
 *
 * External initialization API. It validates the configuration, prewarps each
 * center frequency, applies trapezoidal discretization in double precision,
 * rejects float-rounded poles that fail strict Jury stability conditions, and
 * computes anti-windup shares and alpha. It performs transcendental operations
 * and is not suitable for the sample loop. Failure leaves @p model unchanged.
 *
 * @param[out] model Caller-owned model destination.
 * @param[in] config Complete controller configuration.
 * @return ESP_OK on success, or ESP_ERR_INVALID_ARG for null pointers,
 *         nonfinite/invalid values, too many branches, frequencies at/above
 *         Nyquist, unrepresentable coefficients, or a marginal/unstable
 *         float-rounded branch.
 */
esp_err_t esp_pr_prepare(esp_pr_model_t *model, const esp_pr_config_t *config);

/**
 * @brief Initialize one independent controller from a prepared model.
 *
 * External initialization API. All dynamic states and diagnostics start at
 * zero. The model is referenced and must remain immutable and alive. Failure
 * leaves @p controller unchanged.
 *
 * @param[out] controller Caller-owned controller state.
 * @param[in] model Successfully prepared model.
 * @return ESP_OK on success, or ESP_ERR_INVALID_ARG for a null destination or
 *         invalid/unprepared model.
 */
esp_err_t esp_pr_init(esp_pr_t *controller, const esp_pr_model_t *model);

/**
 * @brief Construct explicit resonator descriptions from harmonic orders.
 *
 * External convenience API. Entry i receives frequency fundamental_hz*orders[i]
 * together with kr[i] and damping_hz[i]. Validation against the sample-rate
 * Nyquist limit remains the responsibility of esp_pr_prepare(). Failure leaves
 * the entire destination unchanged.
 *
 * @param[out] destination Array with room for @p count branch descriptions.
 * @param count Number of entries; must be in [1, ESP_PR_MAX_RESONATORS].
 * @param fundamental_hz Finite positive fundamental frequency in hertz.
 * @param[in] orders Positive integer harmonic order for each entry.
 * @param[in] kr Nonnegative finite branch gain for each entry.
 * @param[in] damping_hz Finite positive damping frequency for each entry.
 * @return ESP_OK on success, or ESP_ERR_INVALID_ARG for any invalid pointer,
 *         count, fundamental, generated frequency, gain, or damping value.
 */
esp_err_t esp_pr_make_harmonics(esp_pr_resonator_config_t *destination,
                                size_t count, float fundamental_hz,
                                const unsigned *orders, const float *kr,
                                const float *damping_hz);

/**
 * @brief Clear controller states and diagnostics without changing its model.
 *
 * External lifecycle API. It is placed in IRAM, performs bounded work over the
 * fixed maximum 16 entries, and must not race with update or diagnostic reads.
 * Reset is not a bumpless transfer operation.
 *
 * @param[in,out] controller Initialized controller instance.
 * @return ESP_OK on success, or ESP_ERR_INVALID_STATE for an invalid instance.
 */
esp_err_t esp_pr_reset(esp_pr_t *controller);

/**
 * @brief Advance the controller by exactly one configured sample interval.
 *
 * External real-time API. It computes error=reference-measurement, advances all
 * branches, clamps the sum to model limits, and corrects in-phase branch states
 * for the next sample when anti-windup is enabled. It allocates nothing and
 * performs no division, transcendental operation, logging, or locking.
 *
 * A single owner must serialize calls. @p output must not alias controller or
 * model storage. On invalid/nonfinite input or intermediate overflow, neither
 * controller nor *output changes; the caller selects the fallback command.
 *
 * @param[in,out] controller Initialized controller instance in mutable internal
 *                           RAM when cache-independent execution is required.
 * @param reference Desired input in application-defined units.
 * @param measurement Measured input in the same units as @p reference.
 * @param[out] output Applied saturated command in controller-output units.
 * @return ESP_OK on success; ESP_ERR_INVALID_STATE for an invalid instance; or
 *         ESP_ERR_INVALID_ARG for null output, nonfinite input, or nonfinite
 *         intermediate arithmetic.
 */
esp_err_t esp_pr_update(esp_pr_t *controller, float reference,
                        float measurement, float *output);

/**
 * @brief Copy diagnostics from the latest successful update.
 *
 * External task-context API. The copy is not synchronized; callers reading an
 * instance owned by another task/core must provide synchronization or consume
 * a coherently published copy. This function is not called internally.
 *
 * @param[in] controller Initialized controller instance.
 * @param[out] diagnostics Destination snapshot.
 * @return ESP_OK on success; ESP_ERR_INVALID_STATE for an invalid instance; or
 *         ESP_ERR_INVALID_ARG for a null destination.
 */
esp_err_t esp_pr_get_diagnostics(const esp_pr_t *controller,
                                 esp_pr_diagnostics_t *diagnostics);

#ifdef __cplusplus
}
#endif
