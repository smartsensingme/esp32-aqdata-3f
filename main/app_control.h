#pragma once

#include "app_parallel.h"
#include "esp_attr.h"
#include <stdbool.h>
#include <stdint.h>

// Provisional R AC commissioning gains; validate before normal/DC operation.
#define APP_CONTROL_KP 0.5f
#define APP_CONTROL_KR60 14.0f
#define APP_CONTROL_KR120 1.0f
#define APP_CONTROL_KR180 3.0f
#define APP_CONTROL_KR300 2.0f
#define APP_CONTROL_KR420 1.0f
#define APP_CONTROL_KI 10.0f

// Constants for Outgoing Control Effort (Telemetry)
#define RMS_BUFFER_SIZE 1440 // 100 ms: 6 ciclos de 60 Hz / 5 de 50 Hz

// Faixa bipolar representada pelos 12 bits enviados ao DAC.
#define CONTROL_OUTPUT_MIN_V -6.0f
#define CONTROL_OUTPUT_MAX_V 6.0f
#define DAC_MAX_CODE 4095U
// Midpoint code for zero bipolar action, independent of ADC input offsets.
#define DAC_NEUTRAL_CODE 2048U
// Carga (Ohms) que transforma a tensão do condicionador em corrente.
#define R_LOAD 1985.0f
// Measured common Vref=3.36 V. Conditioner gain=2 remains the schematic assumption.
#define DAC_REFERENCE_V 3.36f
#define CONDITIONER_REFERENCE_V 3.36f
// Números de espiras da bonina
#define N_COIN_TURNS 2000.0f

// State exposed to main.c
extern app_parallel_t pr_fase_r;
extern app_parallel_t pr_fase_s;
extern app_parallel_t pr_fase_t;

/**
 * @brief Inicializa os controladores PR + I (esp_pid) das tres fases.
 *
 * PR e I usam toda a faixa, com tracking comum da soma aplicada e zera os estados das
 * fases R, S e T. Deve ser chamada antes de iniciar o laco de controle.
 */
void app_control_init(void);
// Prepare the symmetric software command bound before ADC acquisition.
void app_control_init_with_limit(float limit);

/**
 * @brief Soma PR e PID integral, com tracking comum da saturacao.
 *
 * Processa o erro atual usando o estado da fase indicada e satura a soma das
 * parcelas proporcional e ressonantes no limite simetrico configurado por
 * CONFIG_APP_CONTROL_LIMIT_MV (escala de comando do software).
 * O coordenador limita a soma e corrige os estados PR/I com uma unica
 * diferenca de saturacao, sem reservas de faixa por ramo. As correcoes
 * locais sao desativadas. O proporcional esta apenas no PR (PID Kp=Kd=0).
 * Em falha numerica, reinicializa apenas esta fase e retorna 0 V.
 *
 * @param controller Ponteiro para o estado do controlador da fase processada.
 * @param error Erro filtrado atual da malha, em volts.
 * @return Acao de controle limitada a +/-CONFIG_APP_CONTROL_LIMIT_MV/1000,
 *         na escala de volts do software, ou zero em falha numerica.
 */
float app_control_process_phase(app_parallel_t *controller, float error);

/**
 * @brief Converte uma acao de controle bipolar para o codigo do DAC de 12 bits.
 *
 * Aplica saturacao defensiva e realiza o mapeamento linear:
 * CONTROL_OUTPUT_MIN_V -> 0, 0 V -> 2048 e CONTROL_OUTPUT_MAX_V -> 4095.
 * O resultado e arredondado para o inteiro mais proximo. Entradas nao finitas
 * produzem o codigo de 0 V (2048).
 *
 * @note A faixa +/-6 V e uma escala interna de comando, nao a tensao
 *       fisica da bobina. A estimativa fisica usa as referencias e Rs.
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

/* Same bounded atomic capture as store_telemetry, with raw and FIR-normalized
 * ADC volts. Arrays contain R/S/T and are copied synchronously when requested.
 */
bool app_control_store_sample(const uint16_t commands[3], const float adc[3],
                              const float filtered_adc[3]);

/* Single RMS consumer only. Returns false without changing outputs if no
 * complete capture exists; otherwise computes RMS and releases the buffer.
 * Output pointers may be NULL. No new capture is accepted during the read. */
bool app_control_calc_rms(float *rms_r, float *rms_s, float *rms_t);

/* Estimated primary-equivalent compensation current from the actual DAC code.
 * MCP4922 uses denominator 4096; Vac=2*VDAC-Vref, Is=Vac/Rs.
 * Assumes 2000 compensation turns / one primary turn and unity current servo.
 * This is NOT a direct measurement of primary current while residual error
 * exists. */
static inline float IRAM_ATTR
app_control_dac_to_primary_current(uint16_t code) {
  const float voltage = 2.0f * DAC_REFERENCE_V * ((float)code / 4096.0f) -
                        CONDITIONER_REFERENCE_V;
  return voltage * (N_COIN_TURNS / R_LOAD);
}
