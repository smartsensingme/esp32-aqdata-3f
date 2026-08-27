#include "fir_pmr.h"
#include <stddef.h>
#include "esp_attr.h"

/* FIR Parks-McClellan gerado pelo GNU Octave.
 * Fs = 14400 Hz, ordem = 14, 15 coeficientes, atraso = 7 amostras.
 *
 * Os coeficientes ficam na DRAM porque fir_pmr_process() faz parte do caminho
 * de controle em tempo real e deve continuar acessivel quando o cache da flash
 * estiver temporariamente desabilitado.
 */
static DRAM_ATTR const float fir_h[FIR_PMR_HALF + 1] = {
    -2.649769434988316e-02f,
    -1.973714929907618e-02f,
    -5.132740945614728e-03f,
    3.129857530427908e-02f,
    8.558767456757677e-02f,
    1.446596688184109e-01f,
    1.906123538202989e-01f,
    2.079326067165584e-01f,
};

/* Soma dos 15 coeficientes. Usada somente para inicializar o historico com a
 * primeira amostra sem introduzir um transitorio artificial de zeros. */
#define FIR_PMR_DC_GAIN 1.009513983f

void fir_pmr_init(fir_pmr_state_t *state) {
  state->head = 0;
  state->initialized = false;
}

float IRAM_ATTR fir_pmr_process(fir_pmr_state_t *state, float sample) {
  if (!state->initialized) {
    /* Preenche as duas copias do buffer com a primeira amostra. */
    for (size_t i = 0; i < FIR_PMR_HISTORY_SIZE; ++i) {
      state->history[i] = sample;
    }
    state->head = 0;
    state->initialized = true;
    return sample * FIR_PMR_DC_GAIN;
  }

  /*
   * Buffer circular espelhado: cada amostra e escrita em duas posicoes. Isso
   * custa uma escrita extra, mas fornece sempre uma janela contigua x[0..14]
   * e elimina modulo/divisao do caminho executado a 14,4 kHz.
   */
  state->head = (state->head == 0) ? (FIR_PMR_TAPS - 1) : (state->head - 1);
  state->history[state->head] = sample;
  state->history[state->head + FIR_PMR_TAPS] = sample;

  const float *x = &state->history[state->head];

  /* h[k] == h[14-k]: 15 taps sao calculados com apenas 8 multiplicacoes. */
  float y = fir_h[0] * (x[0] + x[14]);
  y += fir_h[1] * (x[1] + x[13]);
  y += fir_h[2] * (x[2] + x[12]);
  y += fir_h[3] * (x[3] + x[11]);
  y += fir_h[4] * (x[4] + x[10]);
  y += fir_h[5] * (x[5] + x[9]);
  y += fir_h[6] * (x[6] + x[8]);
  y += fir_h[7] * x[7];

  return y;
}
