#pragma once

/**
 * @file esp_fir.h
 * @brief Allocation-free real FIR filters for bounded-latency sample loops.
 *
 * The API separates an immutable prepared model from per-stream dynamic state.
 * A model may be shared by multiple filters, but every filter must own a
 * distinct history array. Preparation, initialization, and reset are intended
 * for task context; the processing functions are the real-time hot path.
 */

#include <stdbool.h>
#include <stddef.h>

#ifdef ESP_PLATFORM
#include "esp_attr.h"
#include "esp_err.h"
/** Places sample-processing code in IRAM in ESP-IDF builds. */
#define ESP_FIR_IRAM IRAM_ATTR
#else
/** Empty host-test replacement for the ESP-IDF IRAM attribute. */
#define ESP_FIR_IRAM
/** Host-test replacement for ESP-IDF's error-code type. */
typedef int esp_err_t;
/** Host-test success code. */
#define ESP_OK 0
/** Host-test invalid-argument code. */
#define ESP_ERR_INVALID_ARG 0x102
/** Host-test invalid-state code. */
#define ESP_ERR_INVALID_STATE 0x103
/** Host-test invalid-size code. */
#define ESP_ERR_INVALID_SIZE 0x104
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Impulse-response structure and corresponding processing kernel. */
typedef enum {
  /** Arbitrary real coefficients; uses one multiplication per tap. */
  ESP_FIR_GENERAL,
  /** h[k] == h[N-1-k]; uses ceil(N/2) multiplications. */
  ESP_FIR_SYMMETRIC,
  /** h[k] == -h[N-1-k]; uses floor(N/2) multiplications. */
  ESP_FIR_ANTISYMMETRIC,
} esp_fir_structure_t;

/** @brief Layout of the caller-owned coefficient array. */
typedef enum {
  /** Store all coefficients in order from h[0] through h[N-1]. */
  ESP_FIR_COEFFICIENTS_FULL,
  /**
   * Store h[0] through h[floor(N/2)-1]. For an odd symmetric FIR, append
   * h[floor(N/2)]. An odd antisymmetric FIR has an implicit zero center.
   * General filters do not support this layout.
   */
  ESP_FIR_COEFFICIENTS_FOLDED,
} esp_fir_coefficient_layout_t;

/** @brief Assumption for samples preceding the first processed sample. */
typedef enum {
  /** Treat all earlier samples as zero, producing the normal FIR startup. */
  ESP_FIR_STARTUP_ZERO,
  /**
   * Treat all earlier samples as equal to the first input. This suppresses the
   * artificial zero-history transient for a signal already at a constant
   * value; the first result is first_sample multiplied by the FIR DC gain.
   */
  ESP_FIR_STARTUP_PRIME_FIRST_SAMPLE,
} esp_fir_startup_t;

/**
 * @brief User configuration consumed by esp_fir_prepare().
 *
 * The structure itself may be temporary. The coefficient data is referenced,
 * not copied, and must remain immutable for the lifetime of the prepared model
 * and every filter using that model.
 */
typedef struct {
  /** Finite real coefficients in the layout selected below. */
  const float *coefficients;
  /** Number of float elements available at coefficients. */
  size_t coefficient_count;
  /** Number N of FIR taps; must be greater than zero. */
  size_t tap_count;
  /** Mathematical structure promised by the coefficients. */
  esp_fir_structure_t structure;
  /** Full or folded storage convention for coefficients. */
  esp_fir_coefficient_layout_t coefficient_layout;
  /** Initial-history policy applied by init and reset. */
  esp_fir_startup_t startup;
  /**
   * Absolute tolerance for validating mirrored pairs in a FULL symmetric or
   * antisymmetric array. Must be finite and nonnegative. Ignored for a folded
   * array because only one member of each pair is stored.
   */
  float symmetry_tolerance;
} esp_fir_config_t;

/**
 * @brief Immutable prepared FIR description shared by filter instances.
 *
 * Fields are public to permit caller-controlled static allocation. Treat every
 * field as read-only after successful preparation. The model must outlive all
 * filters that reference it and must not be prepared again while they run.
 */
typedef struct {
  /** Borrowed pointer to the immutable caller-owned coefficient array. */
  const float *coefficients;
  /** Full impulse-response length N. */
  size_t tap_count;
  /** Number floor(N/2) of mirrored tap pairs. */
  size_t pair_count;
  /** Sum of the full impulse response, used for first-sample priming. */
  float dc_gain;
  /** Kernel structure selected during preparation. */
  esp_fir_structure_t structure;
  /** Coefficient storage layout selected during preparation. */
  esp_fir_coefficient_layout_t coefficient_layout;
  /** Initial-history policy copied from the configuration. */
  esp_fir_startup_t startup;
  /** True only after successful esp_fir_prepare(). */
  bool prepared;
} esp_fir_model_t;

/**
 * @brief Mutable state for one filtered signal stream.
 *
 * One task or otherwise synchronized owner must serialize processing and reset
 * calls. Do not share an instance or its history across independent streams.
 */
typedef struct {
  /** Borrowed model pointer; the model must outlive this instance. */
  const esp_fir_model_t *model;
  /** Caller-owned mirrored history containing 2*N floats. */
  float *history;
  /** Index of the newest sample in the first history copy. */
  size_t head;
  /** Whether the history already contains a real or primed first sample. */
  bool initialized;
} esp_fir_t;

/**
 * @brief Return the required history capacity for an N-tap filter.
 *
 * External callers use this to size dynamic storage. Internally it is called
 * by esp_fir_prepare(), esp_fir_init(), and esp_fir_reset().
 *
 * @param tap_count Number N of FIR taps.
 * @return Number of float elements required (2*N), or zero when N is zero or
 *         the byte-size calculation would overflow size_t.
 */
size_t esp_fir_history_size(size_t tap_count);

/**
 * @brief Validate a configuration and prepare a shareable model.
 *
 * This external initialization API computes the folded-pair count and DC gain,
 * validates finite coefficients and any promised symmetry, and stores only a
 * pointer to the coefficient array. It performs O(N) work and is not a
 * real-time operation. Failure leaves @p model unchanged.
 *
 * @param[out] model Destination model with caller-managed lifetime.
 * @param[in] config Complete FIR configuration.
 * @return ESP_OK on success; ESP_ERR_INVALID_ARG for null pointers, invalid
 *         enums/counts/tolerance, nonfinite coefficients, unsupported folded
 *         general layout, failed symmetry, zero taps, or size overflow.
 */
esp_err_t esp_fir_prepare(esp_fir_model_t *model,
                          const esp_fir_config_t *config);

/**
 * @brief Bind one stream state to a prepared model and history buffer.
 *
 * This external initialization API clears the required 2*N history elements.
 * It performs O(N) writes and is not a real-time operation. Extra elements in
 * the supplied buffer are untouched. Failure leaves @p filter unchanged.
 *
 * @param[out] filter Destination stream state.
 * @param[in] model Successfully prepared model that outlives @p filter.
 * @param[in,out] history Writable, non-shared array in internal RAM when
 *                       cache-independent processing is required.
 * @param history_length Capacity of @p history in float elements.
 * @return ESP_OK on success; ESP_ERR_INVALID_STATE for an invalid/unprepared
 *         model or null filter; ESP_ERR_INVALID_ARG for null history; or
 *         ESP_ERR_INVALID_SIZE when history is shorter than 2*N.
 */
esp_err_t esp_fir_init(esp_fir_t *filter, const esp_fir_model_t *model,
                       float *history, size_t history_length);

/**
 * @brief Clear a filter's history while retaining its model binding.
 *
 * This external lifecycle API restores the configured startup policy. It
 * performs O(N) writes and must not race with processing.
 *
 * @param[in,out] filter Initialized filter instance.
 * @return ESP_OK on success, or ESP_ERR_INVALID_STATE for an invalid instance.
 */
esp_err_t esp_fir_reset(esp_fir_t *filter);

/**
 * @brief Process one sample using the model-selected kernel.
 *
 * This external convenience API dispatches internally to the matching
 * structure-specific function. Call exactly once for each input sample. The
 * instance must be valid and @p sample finite; these hot-path preconditions are
 * not checked. It allocates nothing and performs no logging, division, modulo,
 * or locking.
 *
 * @param[in,out] filter Initialized instance owned by the caller.
 * @param sample Current scalar input sample in application-defined units.
 * @return Current filtered output in coefficient-scaled input units.
 */
float esp_fir_process(esp_fir_t *filter, float sample);

/**
 * @brief Process one sample with the arbitrary-coefficient kernel.
 *
 * External real-time entry point; also called by esp_fir_process(). The model
 * must have ESP_FIR_GENERAL structure and FULL coefficients. Preconditions are
 * unchecked. Cost is O(N), with N multiplications.
 *
 * @param[in,out] filter Initialized general-filter instance.
 * @param sample Finite current input sample.
 * @return Current FIR output.
 */
float esp_fir_process_general(esp_fir_t *filter, float sample);

/**
 * @brief Process one sample with the symmetric folded-pair kernel.
 *
 * External real-time entry point; also called by esp_fir_process(). The model
 * must have ESP_FIR_SYMMETRIC structure. Preconditions are unchecked. Cost is
 * O(N), with ceil(N/2) multiplications.
 *
 * @param[in,out] filter Initialized symmetric-filter instance.
 * @param sample Finite current input sample.
 * @return Current FIR output.
 */
float esp_fir_process_symmetric(esp_fir_t *filter, float sample);

/**
 * @brief Process one sample with the antisymmetric folded-pair kernel.
 *
 * External real-time entry point; also called by esp_fir_process(). The model
 * must have ESP_FIR_ANTISYMMETRIC structure. Preconditions are unchecked. Cost
 * is O(N), with floor(N/2) multiplications; an odd center tap is implicitly
 * zero.
 *
 * @param[in,out] filter Initialized antisymmetric-filter instance.
 * @param sample Finite current input sample.
 * @return Current FIR output.
 */
float esp_fir_process_antisymmetric(esp_fir_t *filter, float sample);

#ifdef __cplusplus
}
#endif
