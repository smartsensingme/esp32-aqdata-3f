#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef ESP_PLATFORM
#include "esp_attr.h"
#include "esp_err.h"
#define ESP_FIR_IRAM IRAM_ATTR
#else
#define ESP_FIR_IRAM
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_INVALID_SIZE 0x104
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  ESP_FIR_GENERAL,
  ESP_FIR_SYMMETRIC,
  ESP_FIR_ANTISYMMETRIC,
} esp_fir_structure_t;

typedef enum {
  /* coefficients contains every tap h[0] ... h[N-1]. */
  ESP_FIR_COEFFICIENTS_FULL,
  /* Store only h[0] ... h[pairs-1], followed by the center tap for an odd
   * symmetric FIR. Antisymmetric odd filters have an implicit zero center. */
  ESP_FIR_COEFFICIENTS_FOLDED,
} esp_fir_coefficient_layout_t;

typedef enum {
  /* The samples before the first input are zero. */
  ESP_FIR_STARTUP_ZERO,
  /* The samples before the first input equal the first input sample. */
  ESP_FIR_STARTUP_PRIME_FIRST_SAMPLE,
} esp_fir_startup_t;

typedef struct {
  const float *coefficients;
  size_t coefficient_count;
  size_t tap_count;
  esp_fir_structure_t structure;
  esp_fir_coefficient_layout_t coefficient_layout;
  esp_fir_startup_t startup;
  /* Absolute tolerance used to validate symmetry with FULL coefficients. */
  float symmetry_tolerance;
} esp_fir_config_t;

/* The model refers to the caller-owned coefficient array. Keep that array
 * unchanged and in internal RAM while real-time instances use the model. */
typedef struct {
  const float *coefficients;
  size_t tap_count;
  size_t pair_count;
  float dc_gain;
  esp_fir_structure_t structure;
  esp_fir_coefficient_layout_t coefficient_layout;
  esp_fir_startup_t startup;
  bool prepared;
} esp_fir_model_t;

/* Each stream owns one state and a caller-provided mirrored history. The
 * mirrored layout costs 2*N floats and makes the convolution window contiguous
 * without division or modulo in the real-time path. */
typedef struct {
  const esp_fir_model_t *model;
  float *history;
  size_t head;
  bool initialized;
} esp_fir_t;

size_t esp_fir_history_size(size_t tap_count);
esp_err_t esp_fir_prepare(esp_fir_model_t *model,
                          const esp_fir_config_t *config);
esp_err_t esp_fir_init(esp_fir_t *filter, const esp_fir_model_t *model,
                       float *history, size_t history_length);
esp_err_t esp_fir_reset(esp_fir_t *filter);

/* These functions assume a successfully initialized instance and finite input.
 * They allocate nothing, log nothing and perform no division or modulo. Use
 * the structure-specific entry point in a hard real-time loop to avoid the
 * dispatcher branch in esp_fir_process(). */
float esp_fir_process(esp_fir_t *filter, float sample);
float esp_fir_process_general(esp_fir_t *filter, float sample);
float esp_fir_process_symmetric(esp_fir_t *filter, float sample);
float esp_fir_process_antisymmetric(esp_fir_t *filter, float sample);

#ifdef __cplusplus
}
#endif
