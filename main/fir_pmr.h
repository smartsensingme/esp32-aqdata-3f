#pragma once

#include <stdbool.h>
#include <stdint.h>

#define FIR_PMR_TAPS 15
#define FIR_PMR_HALF (FIR_PMR_TAPS / 2)
#define FIR_PMR_DELAY FIR_PMR_HALF
#define FIR_PMR_HISTORY_SIZE (2 * FIR_PMR_TAPS)

typedef struct {
  /* Duas copias do buffer circular permitem acessar os 15 valores
   * cronologicos de forma contigua, sem modulo no calculo da convolucao. */
  float history[FIR_PMR_HISTORY_SIZE];
  uint8_t head;
  bool initialized;
} fir_pmr_state_t;

void fir_pmr_init(fir_pmr_state_t *state);

/* Insere uma amostra e devolve a saida filtrada. */
float fir_pmr_process(fir_pmr_state_t *state, float sample);
