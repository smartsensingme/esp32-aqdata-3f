#pragma once

#include "esp_attr.h"
#include <stdint.h>

#define PR_FS_HZ 14400.0f
#define PR_TS (1.0f / PR_FS_HZ)
#define PR_F0_HZ 60.0f
#define PR_KP 5.0f
#define PR_WC 31.4159265359f
#define PR_NUM_RESONATORS 4U

/**
 * @brief Coeficientes de um ressonador de segunda ordem do controlador PR.
 *
 * Cada elemento descreve a equacao de diferencas:
 * y[k] = b0*x[k] + b1*x[k-1] + b2*x[k-2]
 *        - a1*y[k-1] - a2*y[k-2].
 */
typedef struct {
  uint8_t harmonic; /*!< Ordem da harmonica relativa a PR_F0_HZ. */
  float b0;         /*!< Coeficiente do erro atual x[k]. */
  float b1;         /*!< Coeficiente do erro anterior x[k-1]. */
  float b2;         /*!< Coeficiente do erro x[k-2]. */
  float a1;         /*!< Coeficiente da saida anterior y[k-1]. */
  float a2;         /*!< Coeficiente da saida y[k-2]. */
} pr_resonator_coefficients_t;

/**
 * @brief Estado dinamico de uma instancia do controlador PR multiressonante.
 *
 * Uma instancia deve ser mantida para cada malha independente. Neste projeto,
 * as fases R, S e T possuem estados distintos para impedir que os historicos
 * de erro e de saida sejam compartilhados entre elas.
 */
typedef struct {
  float x1[PR_NUM_RESONATORS]; /*!< Erro x[k-1] de cada ressonador. */
  float x2[PR_NUM_RESONATORS]; /*!< Erro x[k-2] de cada ressonador. */
  float y1[PR_NUM_RESONATORS]; /*!< Saida y[k-1] de cada ressonador. */
  float y2[PR_NUM_RESONATORS]; /*!< Saida y[k-2] de cada ressonador. */
} pr_controller_state_t;

/**
 * @brief Inicializa uma instancia do controlador PR multiressonante.
 *
 * Zera os historicos de entrada e saida de todos os ressonadores. Deve ser
 * chamada uma vez antes do primeiro processamento da instancia.
 *
 * @param state Ponteiro para o estado do controlador a ser inicializado.
 */
void pr_controller_init(pr_controller_state_t *state);

/**
 * @brief Processa uma amostra de erro pelo controlador PR multiressonante.
 *
 * Calcula a parcela proporcional PR_KP*error, processa em paralelo os
 * ressonadores das harmonicas configuradas e soma todas as contribuicoes.
 * A funcao tambem atualiza os historicos contidos em @p state.
 *
 * @note A saida desta funcao nao possui saturacao. O limite fisico de
 *       [-6 V, +6 V] e aplicado por calcular_controle_PR().
 *
 * @param state Ponteiro para o estado da malha que sera processada.
 * @param error Erro atual da malha, em volts.
 * @return Acao de controle multiressonante, em volts, antes da saturacao.
 */
float pr_controller_process(pr_controller_state_t *state, float error);
