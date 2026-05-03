#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_attr.h"

// Constants for conditioning
#define OFFSET_R -2.9f
#define OFFSET_S -2.9f
#define OFFSET_T -2.9f

// Constants for Outgoing Control Effort (Telemetry)
#define RMS_BUFFER_SIZE 675 // 3 ciclos de 60Hz

// Ganho do amplificador de condicionamento do sinal do DAC
#define OUT_GAIN 2.0f
// Offset do amplificador de condicionamento do sinal do DAC
#define OUT_OFFSET -3.3f
// Carga (Ohms) que transforma a tensão do condicionador em corrente.
#define R_LOAD 150.0f
// Números de espiras da bonina
#define N_COIN_TURNS 2000.0f

// PR Controller Constants (Calculated via Octave for Bits Domain)
#define PR_B0 12.532f
#define PR_B1 -24.115f
#define PR_B2 11.601f
#define PR_A1 -1.995f
#define PR_A2 0.998f
#define PR_OFFSET -1.650f

// Indices for phase shifting (3-phase network)
#define PHASE_0_OFFSET 0
#define PHASE_120_OFFSET 75  // 120 degrees = 1/3 of 225 points
#define PHASE_240_OFFSET 150 // 240 degrees = 2/3 of 225 points

#define SINE_POINTS 225

typedef struct {
  float e[3]; // Histórico de erros: [0]=atual, [1]=k-1, [2]=k-2
  float u[3]; // Histórico de ações: [0]=atual, [1]=k-1, [2]=k-2
} PR_Estado_t;

// State exposed to main.c
extern PR_Estado_t pr_fase_r;
extern PR_Estado_t pr_fase_s;
extern PR_Estado_t pr_fase_t;

// Dummy Sine Wave LUT
extern uint16_t sine_lut[SINE_POINTS];

// Telemetry state exposed for task notification trigger
extern volatile bool capture_requested;
extern volatile int capture_idx;

// Initializes the PR states and pre-computes the Sine LUT
void app_control_init(void);

// Encapsulates the PR equation
float calcular_controle_PR(PR_Estado_t *estado, float erro_atual);

// Encapsulates telemetry storage (returns true when buffer is full)
bool app_control_store_telemetry(uint16_t val_r, uint16_t val_s, uint16_t val_t);

// RMS mathematical loop for the Core 0 task
void app_control_calc_rms(void);
