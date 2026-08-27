#include "pr_controller.h"
#include <stddef.h>

/* Coeficientes gerados pelo GNU Octave para Fs = 14,4 kHz, fundamental de
 * 60 Hz e ressonadores nas harmonicas 1, 3, 5 e 7. O vetor fica em DRAM para
 * permanecer acessivel no caminho de controle sem depender do cache flash. */
static DRAM_ATTR const pr_resonator_coefficients_t pr_coefficients[] = {
    /* h, b0, b1, b2, a1, a2 */
    {1, 130.599849414f, 0.0f, -130.599849414f, -1.99496281341f,
     0.995646671686f},
    {3, 13.0480787197f, 0.0f, -13.0480787197f, -1.98949871552f,
     0.995650640427f},
    {5, 21.7071428583f, 0.0f, -21.7071428583f, -1.9785854357f,
     0.995658571428f},
    {7, 28.1420636624f, 0.0f, -28.1420636624f, -1.96225276556f,
     0.995670451744f},
};

_Static_assert(sizeof(pr_coefficients) / sizeof(pr_coefficients[0]) ==
                   PR_NUM_RESONATORS,
               "PR_NUM_RESONATORS must match the coefficient table");

void pr_controller_init(pr_controller_state_t *state) {
  for (size_t i = 0; i < PR_NUM_RESONATORS; ++i) {
    state->x1[i] = 0.0f;
    state->x2[i] = 0.0f;
    state->y1[i] = 0.0f;
    state->y2[i] = 0.0f;
  }
}

float IRAM_ATTR pr_controller_process(pr_controller_state_t *state,
                                      float error) {
  float output = PR_KP * error;

  for (size_t i = 0; i < PR_NUM_RESONATORS; ++i) {
    const pr_resonator_coefficients_t *coeff = &pr_coefficients[i];
    const float y = coeff->b0 * error + coeff->b1 * state->x1[i] +
                    coeff->b2 * state->x2[i] - coeff->a1 * state->y1[i] -
                    coeff->a2 * state->y2[i];

    state->x2[i] = state->x1[i];
    state->x1[i] = error;
    state->y2[i] = state->y1[i];
    state->y1[i] = y;
    output += y;
  }

  return output;
}
