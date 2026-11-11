#pragma once

#include "esp_attr.h"
#include "esp_pr.h"
#include <stdbool.h>
#include <stdint.h>

// Constants for conditioning
#define OFFSET_R -2.9f
#define OFFSET_S -2.9f
#define OFFSET_T -2.9f

// Constants for Outgoing Control Effort (Telemetry)
#define RMS_BUFFER_SIZE 720 // 3 ciclos de 60Hz @ 14.4 kHz

// Faixa bipolar representada pelos 12 bits enviados ao DAC.
#define CONTROL_OUTPUT_MIN_V -6.0f
#define CONTROL_OUTPUT_MAX_V 6.0f
#define DAC_MAX_CODE 4095U
// Carga (Ohms) que transforma a tensão do condicionador em corrente.
#define R_LOAD 150.0f
// Números de espiras da bonina
#define N_COIN_TURNS 2000.0f

// State exposed to main.c
extern esp_pr_t pr_fase_r;
extern esp_pr_t pr_fase_s;
extern esp_pr_t pr_fase_t;

/**
 * @brief Inicializa os controladores PR das tres fases.
 *
 * Zera, de forma independente, os estados dos controladores associados as
 * fases R, S e T. Deve ser chamada antes de iniciar o laco de controle.
 */
void app_control_init(void);

/**
 * @brief Calcula e limita a acao do controlador PR multiressonante.
 *
 * Processa o erro atual usando o estado da fase indicada e satura a soma das
 * parcelas proporcional e ressonantes na faixa fisica permitida pelo atuador.
 * A diferenca de saturacao realimenta os estados ressonantes por anti-windup.
 * Em falha numerica, reinicializa apenas esta fase e retorna 0 V.
 *
 * @param controller Ponteiro para o estado do controlador da fase processada.
 * @param error Erro filtrado atual da malha, em volts.
 * @return Acao de controle saturada entre CONTROL_OUTPUT_MIN_V e
 *         CONTROL_OUTPUT_MAX_V, em volts.
 */
float app_control_process_phase(esp_pr_t *controller, float error);

/**
 * @brief Converte uma acao de controle bipolar para o codigo do DAC de 12 bits.
 *
 * Aplica saturacao defensiva e realiza o mapeamento linear:
 * CONTROL_OUTPUT_MIN_V -> 0, 0 V -> 2048 e CONTROL_OUTPUT_MAX_V -> 4095.
 * O resultado e arredondado para o inteiro mais proximo. Entradas nao finitas
 * produzem o codigo de 0 V (2048).
 *
 * @note O codigo representa a tensao bipolar que devera existir depois do
 *       circuito analogico de condicionamento. O MCP4922 permanece unipolar.
 *
 * @param control_voltage Acao de controle desejada, em volts.
 * @return Codigo inteiro de 12 bits no intervalo [0, DAC_MAX_CODE].
 */
uint16_t app_control_voltage_to_dac(float control_voltage);

/* RMS-worker only: request a capture; false if one is pending or unread.
 * Control task exclusively owns the sample index and buffer writes. */
bool app_control_request_capture(void);

// Control-task only; returns true exactly once when a buffer becomes ready.
bool app_control_store_telemetry(uint16_t val_r, uint16_t val_s,
                                 uint16_t val_t);

/* Single RMS consumer only. Returns false without changing outputs if no
 * complete capture exists; otherwise computes RMS and releases the buffer.
 * Output pointers may be NULL. No new capture is accepted during the read. */
bool app_control_calc_rms(float *rms_r, float *rms_s, float *rms_t);
