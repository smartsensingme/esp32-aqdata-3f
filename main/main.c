#include "app_control.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_continuous.h"
#include "esp_clk_tree.h"
#include "esp_cpu.h"
#include "esp_log.h"
#include "esp_mcp4922.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/apb_saradc_struct.h"
#include "soc/clk_tree_defs.h"
#include <math.h>
#include <stdio.h>

#include "app_mqtt.h"
#include "fir_pmr.h"

static const char *TAG = "main";

#define NUM_CHIPS 2

// ADC Channels (using ADC1 on ESP32-S3: GPIO4, GPIO5, GPIO6)
#define ADC_CHAN_R ADC_CHANNEL_3
#define ADC_CHAN_S ADC_CHANNEL_4
#define ADC_CHAN_T ADC_CHANNEL_5

static mcp4922_context_t dac_ctx;

static TaskHandle_t control_task_handle = NULL;
static TaskHandle_t rms_task_handle = NULL;

// Dados sobre o loop de controle (apenas para estatísticas)
typedef struct {
  volatile uint32_t generated_frames;     // Frames sinalizados pela ISR.
  volatile uint32_t processed_frames;     // Frames completos levados ao DAC.
  volatile uint32_t backlog_events;       // Episodios de fila com >1 frame.
  volatile uint32_t max_pending_frames;   // Maior profundidade observada.
  volatile uint32_t deadline_misses;      // Iteracoes acima de 69,44 us.
  volatile uint32_t adc_read_errors;      // Frames que nao puderam ser lidos.
  volatile uint32_t adc_partial_reads;    // Fragmentos remontados com sucesso.
  volatile uint32_t adc_pool_overflows;   // Frames perdidos por pool cheio.
  volatile uint32_t max_execution_cycles; // Pior tempo desde o boot.
} control_stats_t;

/*
 * Cada campo possui apenas um escritor (task de controle ou ISR) e e lido
 * apenas para diagnostico pela task do Core 0. Leituras/escritas alinhadas de
 * 32 bits sao atomicas no ESP32-S3; pequenas diferencas entre os instantes do
 * snapshot nao afetam as estatisticas nem adicionam locks ao caminho critico.
 */
static DRAM_ATTR control_stats_t control_stats;

// ADC Handles
static adc_continuous_handle_t adc_handle = NULL;
static adc_cali_handle_t adc_cali_handle = NULL;
static bool do_calibration = false;

// Calibration Lookup Table (16KB) to bypass FreeRTOS mutexes during 14.4kHz
// loop
static float adc_cali_lut[4096];

// 14.4 kHz per phase gives exactly 240 samples per 60 Hz cycle. With the
// R/S/T pattern, the ADC must perform 43.2 k conversions per second.
#define ADC_CONVERSIONS_PER_FRAME 3U
#define ADC_TARGET_FRAME_FREQ_HZ 14400.0f
#define ADC_TARGET_CONVERSION_FREQ_HZ                                          \
  ((uint32_t)(ADC_TARGET_FRAME_FREQ_HZ * ADC_CONVERSIONS_PER_FRAME))
#define CONTROL_PERIOD_CYCLES (configCPU_CLOCK_HZ / 14400U)
#define CONTROL_STATS_PERIOD_MS 5000U

// Se a malha de controle estiver fechada, faça TEST_IS_ON = 0
#define TEST_IS_ON 1

#if TEST_IS_ON != 0 && TEST_IS_ON != 1
#error "TEST_IS_ON must be either 0 or 1"
#endif

#if TEST_IS_ON
#define TEST_SINE_POINTS 240U
#define TEST_PHASE_S_OFFSET 160U // -120 degrees
#define TEST_PHASE_T_OFFSET 80U  // +120 degrees

static uint16_t test_sine_lut[TEST_SINE_POINTS];

static void test_sine_lut_init(void) {
  for (uint32_t i = 0; i < TEST_SINE_POINTS; i++) {
    const float angle = (2.0f * (float)M_PI * (float)i) /
                        (float)TEST_SINE_POINTS;
    test_sine_lut[i] =
        (uint16_t)((sinf(angle) + 1.0f) * ((float)DAC_MAX_CODE / 2.0f) + 0.5f);
  }
}
#endif

/**
 * @brief Reads the ESP32-S3 ADC digital-controller registers and calculates
 *        the configured conversion rate.
 *
 * The continuous-mode ADC is clocked by a source selected in
 * APB_SARADC.apb_adc_clkm_conf. That source is divided by the fractional
 * CLKM divider and then by twice the trigger interval in ctrl2.timer_target.
 * This reports the rate programmed in hardware, relative to its clock source.
 */
static bool adc_get_configured_conversion_freq(float *out_freq_hz) {
  const uint32_t clk_sel = APB_SARADC.apb_adc_clkm_conf.clk_sel;
  soc_module_clk_t clock_source;

  // adc_ll_digi_clk_sel() encodes PLL_D2 as 1 and APB as 2 on ESP32-S3.
  if (clk_sel == 1) {
    clock_source = SOC_MOD_CLK_PLL_D2;
  } else if (clk_sel == 2) {
    clock_source = SOC_MOD_CLK_APB;
  } else {
    ESP_LOGW(TAG, "ADC clock source selection %lu is unsupported", clk_sel);
    return false;
  }

  uint32_t source_freq_hz = 0;
  if (esp_clk_tree_src_get_freq_hz(clock_source,
                                   ESP_CLK_TREE_SRC_FREQ_PRECISION_CACHED,
                                   &source_freq_hz) != ESP_OK) {
    ESP_LOGW(TAG, "Unable to read ADC clock source frequency");
    return false;
  }

  const uint32_t div_num = APB_SARADC.apb_adc_clkm_conf.clkm_div_num;
  const uint32_t div_a = APB_SARADC.apb_adc_clkm_conf.clkm_div_a;
  const uint32_t div_b = APB_SARADC.apb_adc_clkm_conf.clkm_div_b;
  const uint32_t interval = APB_SARADC.ctrl2.timer_target;

  if (div_b == 0 || interval == 0) {
    ESP_LOGW(TAG, "Invalid ADC clock configuration (div_b=%lu, interval=%lu)",
             div_b, interval);
    return false;
  }

  const double clock_divider =
      (double)div_num + ((double)div_a / (double)div_b) + 1.0;
  const double conversion_freq_hz =
      (double)source_freq_hz / (clock_divider * 2.0 * (double)interval);

  *out_freq_hz = (float)conversion_freq_hz;
  ESP_LOGI(TAG,
           "ADC registers: src=%lu Hz, CLKM=%lu + %lu/%lu + 1, interval=%lu",
           source_freq_hz, div_num, div_a, div_b, interval);
  return true;
}

/**
 * @brief Apply the ADC controller registers for the selected target rate.
 *
 * APB = 80 MHz, CLKM = 22 + 4/27 + 1 = 625/27, timer_target = 40:
 * 80 MHz / (625/27) / (2 * 40) = 43.2 kHz.
 *
 * This must run after adc_continuous_start(), because that ESP-IDF call
 * programs the controller registers from its high-level configuration.
 */
static void adc_apply_target_frame_rate(void) {
  APB_SARADC.apb_adc_clkm_conf.clk_sel = 2; // ADC_DIGI_CLK_SRC_APB
  APB_SARADC.apb_adc_clkm_conf.clkm_div_num = 22;
  APB_SARADC.apb_adc_clkm_conf.clkm_div_b = 27;
  APB_SARADC.apb_adc_clkm_conf.clkm_div_a = 4;
  APB_SARADC.ctrl2.timer_target = 40;
}

// ADC_ATTEN_DB_0 (0 dB):
//      Fator: Nenhuma atenuação (o sinal passa direto para o núcleo de 1.1V).
//      Faixa mensurável recomendada: ~0 mV a 950 mV (0,95 V).
// ADC_ATTEN_DB_2_5 (2,5 dB):
//      Fator: Atenuação leve (reduz o sinal em um fator de aprox. 1,33x).
//      Faixa mensurável recomendada: ~0 mV a 1250 mV (1,25 V).
// ADC_ATTEN_DB_6 (6 dB):
//      Fator: Atenuação média (reduz o sinal pela metade, fator de aprox. 2x).
//      Faixa mensurável recomendada: ~0 mV a 1750 mV (1,75 V).
// ADC_ATTEN_DB_12 (12 dB):
//      Fator: Atenuação máxima (reduz o sinal em um fator de aprox. 3,16x).
//      Faixa mensurável recomendada: ~0 mV a 3100 mV (3,1 V).
#define ADC_ATTENUATION ADC_ATTEN_DB_12

/*
 * ============================================================================
 * CRITICAL ARCHITECTURE WARNING (ISR vs FPU)
 * ============================================================================
 * Do NOT perform floating-point calculations (float/double) inside this ISR!
 *
 * On the ESP32 (Xtensa architecture), the C compiler (GCC) automatically
 * translates ANY `float` operation into hardware FPU instructions (e.g.,
 * mul.s). To minimize interrupt latency, the ESP-IDF FreeRTOS port DOES NOT
 * save the FPU register states (f0-f15) when entering an ISR.
 *
 * If this ISR interrupts a Task that was in the middle of a floating-point
 * calculation, and you do a `float` operation here, the FPU registers will be
 * overwritten ("dirtied"). When the ISR returns, the original Task will read
 * corrupted math data, leading to catastrophic and hard-to-debug system
 * crashes.
 *
 * GOLDEN RULE: The ISR must only clear flags and wake up a High-Priority Task
 * (Deferred Interrupt Pattern). All PI/PID control loops and heavy math MUST
 * be executed inside `control_loop_task`.
 * ============================================================================
 */
static bool IRAM_ATTR adc_on_conv_done(adc_continuous_handle_t handle,
                                       const adc_continuous_evt_data_t *edata,
                                       void *user_data) {
  BaseType_t high_task_awoken = pdFALSE;
  control_stats.generated_frames++;
  // Sempre que o hardware fecha 1 Frame de 3 leituras, ele dispara
  // automaticamente a interrupção adc_on_conv_done. Como 43.200 / 3 = 14.400,
  // essa interrupção é chamada cravada a 14.4 kHz. O único trabalho dela é dar
  // um "cutucão" (Notify) na sua Task de controle.
  vTaskNotifyGiveFromISR(control_task_handle, &high_task_awoken);
  return (high_task_awoken == pdTRUE);
}

static bool IRAM_ATTR adc_on_pool_overflow(
    adc_continuous_handle_t handle, const adc_continuous_evt_data_t *edata,
    void *user_data) {
  (void)handle;
  (void)edata;
  (void)user_data;
  control_stats.adc_pool_overflows++;
  return false;
}

static bool init_adc_calibration(adc_unit_t unit, adc_atten_t atten,
                                 adc_cali_handle_t *out_handle) {
  esp_err_t ret = ESP_FAIL;
  bool calibrated = false;

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
  if (!calibrated) {
    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = unit,
        .atten = atten,
        .bitwidth = ADC_BITWIDTH_12,
    };
    ret = adc_cali_create_scheme_curve_fitting(&cali_config, out_handle);
    if (ret == ESP_OK) {
      calibrated = true;
      ESP_LOGI(TAG, "Calibration scheme: Curve Fitting");
    }
  }
#endif

#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
  if (!calibrated) {
    adc_cali_line_fitting_config_t cali_config = {
        .unit_id = unit,
        .atten = atten,
        .bitwidth = ADC_BITWIDTH_12,
    };
    ret = adc_cali_create_scheme_line_fitting(&cali_config, out_handle);
    if (ret == ESP_OK) {
      calibrated = true;
      ESP_LOGI(TAG, "Calibration scheme: Line Fitting");
    }
  }
#endif

  if (!calibrated) {
    ESP_LOGW(TAG, "eFuse not burnt, skip software calibration");
  }
  return calibrated;
}

static void IRAM_ATTR control_loop_task(void *arg) {
  ESP_LOGI(TAG, "Entering DMA Control Loop on Core %d", xPortGetCoreID());

  // Acquire bus permanently for ultra-fast writing
  // O ESP32-S3 possui dois barramentos SPI. Para o laço de controle não ter que
  // pedir acesso ao barramento a cada interrupção, nós adquirimos um barramento
  // aqui e não o liberamos mais. Qualquer task extra vai ter que usar o
  // barramento SPI.
  spi_device_acquire_bus(dac_ctx.spi_handle, portMAX_DELAY);
  uint16_t val[NUM_CHIPS * 2] = {0};

  // Send a dummy transaction using the high-level API to properly configure the
  // SPI hardware
  mcp4922_write_channels(&dac_ctx, val);

  // Phase Skew Compensation variables (First-Order Hold)
  static float prev_err_r = 0.0f;
  static float prev_err_t = 0.0f;

  // Cada fase precisa de um historico FIR independente.
  static fir_pmr_state_t fir_fase_r;
  static fir_pmr_state_t fir_fase_s;
  static fir_pmr_state_t fir_fase_t;
  fir_pmr_init(&fir_fase_r);
  fir_pmr_init(&fir_fase_s);
  fir_pmr_init(&fir_fase_t);

#if TEST_IS_ON
  uint16_t test_val[NUM_CHIPS * 2] = {0};
  uint32_t test_sine_index = 0;
#endif

  bool backlog_active = false;

  /*
   * Inicie o ADC somente depois que todo o estado do controle estiver pronto.
   * adc_continuous_start() reprograma os registradores, portanto a cadencia
   * exata ainda precisa ser aplicada logo depois dele. Nao deve haver logs ou
   * inicializacao adicional entre esse ponto e a espera pelo primeiro frame.
   */
  ESP_ERROR_CHECK(adc_continuous_start(adc_handle));
  adc_apply_target_frame_rate();

  while (1) {
    /*
     * Consume somente um evento. Se mais frames chegaram enquanto a task
     * estava ocupada, o contador permanece positivo e a proxima iteracao
     * comeca imediatamente, preservando a relacao 1:1 entre notificacao,
     * frame ADC e atualizacao do controle.
     */
    const uint32_t pending_frames =
        ulTaskNotifyTake(pdFALSE, portMAX_DELAY);
    const uint32_t start_cycles = esp_cpu_get_cycle_count();

    if (pending_frames > control_stats.max_pending_frames) {
      control_stats.max_pending_frames = pending_frames;
    }

    if (pending_frames > 1) {
      if (!backlog_active) {
        control_stats.backlog_events++;
        backlog_active = true;
      }
    } else {
      backlog_active = false;
    }

    // 1. Read one complete ADC frame (3 channels * 4 bytes).
    uint8_t result_buf[12];
    uint32_t bytes_collected = 0;
    bool read_failed = false;

    /*
     * O driver usa um byte ring buffer e pode devolver somente o trecho
     * contiguo ate o fim do buffer. Nessa situacao um frame de 12 bytes chega
     * em duas leituras. Nunca avance o controle com um frame parcial.
     */
    while (bytes_collected < sizeof(result_buf)) {
      uint32_t ret_num = 0;
      const uint32_t bytes_remaining =
          sizeof(result_buf) - bytes_collected;
      esp_err_t ret = adc_continuous_read(
          adc_handle, result_buf + bytes_collected, bytes_remaining, &ret_num,
          0);

      if (ret != ESP_OK || ret_num == 0 || ret_num > bytes_remaining) {
        read_failed = true;
        break;
      }

      if (ret_num < bytes_remaining) {
        control_stats.adc_partial_reads++;
      }
      bytes_collected += ret_num;
    }

    if (read_failed) {
      control_stats.adc_read_errors++;
      const uint32_t elapsed_cycles =
          esp_cpu_get_cycle_count() - start_cycles;
      if (elapsed_cycles > control_stats.max_execution_cycles) {
        control_stats.max_execution_cycles = elapsed_cycles;
      }
      if (elapsed_cycles > CONTROL_PERIOD_CYCLES) {
        control_stats.deadline_misses++;
      }
      continue;
    }

    int raw_r = 0, raw_s = 0, raw_t = 0;

    // Parse the DMA frame (ESP32-S3 uses type2 format)
    for (uint32_t i = 0; i < sizeof(result_buf);
         i += SOC_ADC_DIGI_RESULT_BYTES) {
      adc_digi_output_data_t *p = (adc_digi_output_data_t *)&result_buf[i];
      int chan = p->type2.channel;
      int data = p->type2.data;

      if (chan == ADC_CHAN_R)
        raw_r = data;
      else if (chan == ADC_CHAN_S)
        raw_s = data;
      else if (chan == ADC_CHAN_T)
        raw_t = data;
    }

    // Protect against out-of-bounds just in case
    raw_r &= 0x0FFF;
    raw_s &= 0x0FFF;
    raw_t &= 0x0FFF;

    // 2 & 3. Conditioning (FPU Single Precision) - Physical Error in Volts
    // Use the pre-computed Calibration LUT to bypass slow mutexes
    // A conversão de RAW para tensão já foi feita na inicialização do ADC.
    // Agora só precisamos usar o valor convertido para indexar a tabela.
    float err_r = adc_cali_lut[raw_r] - OFFSET_R;
    float err_s = adc_cali_lut[raw_s] - OFFSET_S;
    float err_t = adc_cali_lut[raw_t] - OFFSET_T;

    // --- Phase Skew Compensation via First-Order Hold ---
    // A fase S (do meio) é a nossa referência de tempo.
    // A fase R foi lida antes da S, então extrapolamos 1 dt para o FUTURO (+).
    // A fase T foi lida depois da S, então interpolamos 1 dt para o PASSADO
    // (-). Nota: O compilador otimiza (1.0f / 3.0f) para uma constante na
    // compilação.
    float err_r_aligned = err_r + (err_r - prev_err_r) * (1.0f / 3.0f);
    float err_s_aligned = err_s;
    float err_t_aligned = err_t - (err_t - prev_err_t) * (1.0f / 3.0f);

    // Salva os valores reais atuais para a próxima iteração
    prev_err_r = err_r;
    prev_err_t = err_t;

    // 4. FIR: filtra os sinais ja alinhados no instante da fase S.
    float err_r_filtered = fir_pmr_process(&fir_fase_r, err_r_aligned);
    float err_s_filtered = fir_pmr_process(&fir_fase_s, err_s_aligned);
    float err_t_filtered = fir_pmr_process(&fir_fase_t, err_t_aligned);

    // 5. Proportional-Resonant (PR) Controller
    // Passamos os erros alinhados e filtrados para cada controlador.
    // (pr_fase_x é definido no app_control.c e armazena os valosres passados
    // de erro e ação de controle)
    float control_action_r = calcular_controle_PR(&pr_fase_r, err_r_filtered);
    float control_action_s = calcular_controle_PR(&pr_fase_s, err_s_filtered);
    float control_action_t = calcular_controle_PR(&pr_fase_t, err_t_filtered);

    // 6. Map [-6 V, +6 V] to the unsigned 12-bit DAC range.
    val[0] = app_control_voltage_to_dac(control_action_r);
    val[1] = app_control_voltage_to_dac(control_action_s);
    val[2] = app_control_voltage_to_dac(control_action_t);
    val[3] = 0;

    // 7. Write either the controller outputs or the three-phase test signal.
#if TEST_IS_ON
    uint32_t test_s_index = test_sine_index + TEST_PHASE_S_OFFSET;
    if (test_s_index >= TEST_SINE_POINTS) {
      test_s_index -= TEST_SINE_POINTS;
    }

    uint32_t test_t_index = test_sine_index + TEST_PHASE_T_OFFSET;
    if (test_t_index >= TEST_SINE_POINTS) {
      test_t_index -= TEST_SINE_POINTS;
    }

    test_val[0] = test_sine_lut[test_sine_index];
    test_val[1] = test_sine_lut[test_s_index];
    test_val[2] = test_sine_lut[test_t_index];
    test_val[3] = 0;
    mcp4922_ll_write_channels(&dac_ctx, test_val);

    test_sine_index++;
    if (test_sine_index >= TEST_SINE_POINTS) {
      test_sine_index = 0;
    }
#else
    mcp4922_ll_write_channels(&dac_ctx, val);
#endif

    // Telemetry always records the controller outputs, even in test mode.
    if (app_control_store_telemetry(val[0], val[1], val[2])) {
      xTaskNotifyGive(rms_task_handle);
    }

    const uint32_t elapsed_cycles = esp_cpu_get_cycle_count() - start_cycles;
    control_stats.processed_frames++;
    if (elapsed_cycles > control_stats.max_execution_cycles) {
      control_stats.max_execution_cycles = elapsed_cycles;
    }
    if (elapsed_cycles > CONTROL_PERIOD_CYCLES) {
      control_stats.deadline_misses++;
    }
  }
}

/**
 * @brief Publica, a cada 5 segundos, o diagnostico temporal da malha de
 *        controle.
 *
 * Esta task roda com baixa prioridade no Core 0 e somente le snapshots dos
 * contadores escritos pela ISR do ADC e pela task de controle no Core 1. Ela
 * nao bloqueia a malha, nao zera contadores compartilhados e nao coloca
 * ESP_LOG no caminho critico. Por isso, todos os valores sem parenteses sao
 * acumulados desde o boot. Os valores escritos como "(+N)" sao a diferenca
 * desde o log anterior, normalmente uma janela de 5 segundos.
 *
 * Na cadencia nominal de 14,4 kHz, uma janela completa deve apresentar:
 *
 *   generated delta = 72.000
 *   processed delta = 72.000
 *   max_pending     = 1
 *   erros/overflow  = 0
 *
 * A primeira janela pode conter um pouco menos de 72.000 frames porque o ADC
 * comeca depois da criacao desta task. A segunda janela tambem pode ser alguns
 * milissegundos menor, pois a primeira publicacao consulta e registra, uma
 * unica vez, os registradores de clock do ADC antes de obter seu snapshot.
 * Depois disso, os deltas devem estabilizar em 72.000.
 *
 * Significado dos campos publicados:
 *
 * - generated:
 *   Quantidade de callbacks de frame completo recebidos da DMA do ADC. O
 *   contador e incrementado na ISR antes da notificacao da task de controle.
 *   Ele representa a cadencia do produtor. O callback tambem ocorre quando o
 *   ring buffer esta cheio, portanto generated nao garante que o frame tenha
 *   sido armazenado com sucesso; pool_overflows deve ser verificado junto.
 *
 * - processed:
 *   Quantidade de frames completos de 12 bytes que passaram por leitura ADC,
 *   alinhamento de fase, FIR, PR, conversao e escrita no DAC. Um frame parcial
 *   nunca incrementa este contador. Uma diferenca instantanea de um frame
 *   entre generated e processed e normal: o snapshot pode ocorrer enquanto um
 *   frame esta pendente ou sendo processado. Uma diferenca que cresce a cada
 *   janela indica que o consumidor esta ficando para tras.
 *
 * - backlog_events:
 *   Numero de episodios em que a task encontrou mais de uma notificacao
 *   pendente. Varias iteracoes consecutivas drenando a mesma fila contam como
 *   um unico episodio. Um backlog isolado, sem overflow, significa que a task
 *   recuperou um atraso transitorio. Crescimento continuo indica falta de
 *   margem temporal, ISR longa ou bloqueio do Core 1.
 *
 * - max_pending:
 *   Maior valor retornado por ulTaskNotifyTake(pdFALSE, ...) desde o boot,
 *   antes de consumir uma notificacao. O valor normal e 1. Valores maiores
 *   medem a profundidade maxima da fila, nao a quantidade de frames perdidos.
 *   Este maximo e historico e permanece no log mesmo depois da recuperacao.
 *
 * - deadline_misses:
 *   Numero de iteracoes cujo tempo medido foi maior que um periodo de controle
 *   (69,44 us a 14,4 kHz). A medicao comeca depois que ulTaskNotifyTake retorna
 *   e termina depois da telemetria; inclui leitura ADC, FIR, PR e escrita DAC.
 *   Ela nao inclui a latencia anterior de ISR, escalonamento e despertar da
 *   task. Um miss isolado nao prova perda. Misses acompanhados por backlog ou
 *   overflow indicam que o prazo de tempo real nao esta sendo sustentado.
 *
 * - adc_read_errors:
 *   Numero de notificacoes para as quais nao foi possivel reunir um frame
 *   completo. Pode indicar notificacao sem dados correspondentes, perda previa
 *   no pool ou dessincronizacao entre produtor e consumidor. Deve permanecer
 *   zero durante a operacao normal.
 *
 * - partial_reads:
 *   Numero de vezes em que adc_continuous_read devolveu apenas uma parte dos
 *   bytes ainda necessarios. O codigo remonta o frame antes de processa-lo, de
 *   modo que uma leitura parcial recuperada nao e perda de dados. Crescimento
 *   frequente sugere fragmentacao/wrap do byte ring buffer e merece revisao do
 *   tamanho do buffer, embora o valor atual de 2040 bytes seja multiplo dos 12
 *   bytes de um frame e normalmente mantenha este contador em zero.
 *
 * - pool_overflows:
 *   Numero de frames que a ISR nao conseguiu inserir no ring buffer porque ele
 *   estava cheio. Este campo representa perda real de dados ADC. Qualquer valor
 *   novo deve ser investigado em conjunto com backlog, deadline_misses e a
 *   diferenca entre generated e processed.
 *
 * - max_exec:
 *   Maior tempo de execucao observado desde o boot, em ciclos de CPU e em
 *   microssegundos. Assim como max_pending, e um maximo historico: nao diminui
 *   quando o sistema volta ao normal. O contador de ciclos permite medir
 *   tambem o tempo gasto em interrupcoes que preemptem a task depois do inicio
 *   da iteracao. A subtracao permanece valida mesmo se o contador de 32 bits
 *   der a volta durante a medicao curta de uma iteracao.
 *
 * - budget:
 *   Limite de ciclos correspondente a 1/14.400 s na frequencia configurada da
 *   CPU. A 160 MHz sao 11.111 ciclos, aproximadamente 69,44 us. max_exec acima
 *   de budget incrementa deadline_misses.
 *
 * Guia rapido de diagnostico:
 *
 * - Tudo saudavel:
 *   generated == processed por janela, max_pending == 1 e erros == 0.
 * - Atraso recuperavel:
 *   backlog_events cresce, max_pending > 1, mas pool_overflows continua zero e
 *   generated/processed voltam a ter deltas iguais.
 * - Sobrecarga computacional:
 *   deadline_misses e backlog_events crescem juntos; max_exec supera budget.
 * - Perda confirmada:
 *   pool_overflows cresce. adc_read_errors e diferenca acumulada entre produtor
 *   e consumidor podem crescer como consequencia.
 * - Cadencia incorreta:
 *   generated fica consistentemente diferente de 72.000 por janela depois das
 *   janelas iniciais; verificar clock, divisor e timer_target do ADC.
 *
 * @param arg Nao utilizado.
 */
static void control_stats_task(void *arg) {
  (void)arg;

  TickType_t last_wake_time = xTaskGetTickCount();
  uint32_t previous_generated_frames = 0;
  uint32_t previous_frames = 0;
  uint32_t previous_backlog_events = 0;
  uint32_t previous_deadline_misses = 0;
  uint32_t previous_adc_read_errors = 0;
  uint32_t previous_adc_partial_reads = 0;
  uint32_t previous_adc_pool_overflows = 0;
  bool adc_rate_reported = false;

  while (1) {
    vTaskDelayUntil(&last_wake_time,
                    pdMS_TO_TICKS(CONTROL_STATS_PERIOD_MS));

    /*
     * Esta verificacao e deliberadamente feita aqui, fora da task de controle.
     * Funcoes de clock e ESP_LOG nao devem executar entre o inicio do ADC e o
     * atendimento dos frames no Core 1.
     */
    if (!adc_rate_reported) {
      float configured_conversion_freq_hz = 0.0f;
      if (adc_get_configured_conversion_freq(
              &configured_conversion_freq_hz)) {
        ESP_LOGI(TAG,
                 "ADC configurado: %.3f Hz por conversao, %.3f Hz por frame",
                 (double)configured_conversion_freq_hz,
                 (double)(configured_conversion_freq_hz /
                          ADC_CONVERSIONS_PER_FRAME));
      } else {
        ESP_LOGW(TAG, "Could not estimate ADC sampling rate from registers");
      }
      adc_rate_reported = true;
    }

    /* Snapshot sem locks: cada campo tem 32 bits e apenas um escritor. */
    const uint32_t generated_frames = control_stats.generated_frames;
    const uint32_t processed_frames = control_stats.processed_frames;
    const uint32_t backlog_events = control_stats.backlog_events;
    const uint32_t max_pending_frames = control_stats.max_pending_frames;
    const uint32_t deadline_misses = control_stats.deadline_misses;
    const uint32_t adc_read_errors = control_stats.adc_read_errors;
    const uint32_t adc_partial_reads = control_stats.adc_partial_reads;
    const uint32_t adc_pool_overflows = control_stats.adc_pool_overflows;
    const uint32_t max_execution_cycles =
        control_stats.max_execution_cycles;

    /* Converte ciclos para uma unidade legivel usando o clock configurado. */
    const float max_execution_us =
        ((float)max_execution_cycles * 1000000.0f) /
        (float)configCPU_CLOCK_HZ;
    const float budget_us =
        ((float)CONTROL_PERIOD_CYCLES * 1000000.0f) /
        (float)configCPU_CLOCK_HZ;

    /*
     * Primeiro valor de cada metrica: acumulado desde o boot.
     * Valor (+N): incremento desde o snapshot anterior de 5 segundos.
     */
    ESP_LOGI(
        TAG,
        "Control stats/5s: generated=%lu (+%lu), processed=%lu (+%lu), "
        "backlog_events=%lu (+%lu), max_pending=%lu, deadline_misses=%lu "
        "(+%lu), adc_read_errors=%lu (+%lu), partial_reads=%lu (+%lu), "
        "pool_overflows=%lu (+%lu), max_exec=%lu cycles (%.2f us), budget=%lu "
        "cycles (%.2f us)",
        (unsigned long)generated_frames,
        (unsigned long)(generated_frames - previous_generated_frames),
        (unsigned long)processed_frames,
        (unsigned long)(processed_frames - previous_frames),
        (unsigned long)backlog_events,
        (unsigned long)(backlog_events - previous_backlog_events),
        (unsigned long)max_pending_frames, (unsigned long)deadline_misses,
        (unsigned long)(deadline_misses - previous_deadline_misses),
        (unsigned long)adc_read_errors,
        (unsigned long)(adc_read_errors - previous_adc_read_errors),
        (unsigned long)adc_partial_reads,
        (unsigned long)(adc_partial_reads - previous_adc_partial_reads),
        (unsigned long)adc_pool_overflows,
        (unsigned long)(adc_pool_overflows - previous_adc_pool_overflows),
        (unsigned long)max_execution_cycles, (double)max_execution_us,
        (unsigned long)CONTROL_PERIOD_CYCLES, (double)budget_us);

    /* Salva a base da proxima janela; subtracao unsigned tolera wrap de 32 bits. */
    previous_generated_frames = generated_frames;
    previous_frames = processed_frames;
    previous_backlog_events = backlog_events;
    previous_deadline_misses = deadline_misses;
    previous_adc_read_errors = adc_read_errors;
    previous_adc_partial_reads = adc_partial_reads;
    previous_adc_pool_overflows = adc_pool_overflows;
  }
}

static void rms_calc_task(void *arg) {
  while (1) {
    vTaskDelay(pdMS_TO_TICKS(4000));

    capture_idx = 0;
    capture_requested = true;

    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    float rms_r = 0.0f, rms_s = 0.0f, rms_t = 0.0f;
    app_control_calc_rms(&rms_r, &rms_s, &rms_t);

#if RUN_WITH_WIFI
    app_mqtt_publish_rms(rms_r, rms_s, rms_t);
#endif
  }
}

void app_main(void) {
  ESP_LOGI(TAG, "Starting Digital Control Loop Architecture (ESP32-S3)");

#if RUN_WITH_WIFI
  app_mqtt_init();
#endif

  app_control_init();
#if TEST_IS_ON
  test_sine_lut_init();
  ESP_LOGW(TAG,
           "DAC test mode enabled: outputting three-phase 60 Hz sine waves");
#endif

  // Configure MCP4922
  gpio_num_t cs_pins[NUM_CHIPS] = {CONFIG_MCP4922_CS1_PIN,
                                   CONFIG_MCP4922_CS2_PIN};

  mcp4922_config_t dac_config = {.host_id = SPI2_HOST,
                                 .mosi_io_num = CONFIG_MCP4922_MOSI_PIN,
                                 .sck_io_num = CONFIG_MCP4922_SCK_PIN,
                                 .ldac_io_num = CONFIG_MCP4922_LDAC_PIN,
                                 .num_chips = NUM_CHIPS,
                                 .cs_pins = cs_pins,
                                 .gain_2x = false,
                                 .vref_buffered = true};

  esp_err_t ret = mcp4922_init(&dac_config, &dac_ctx);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize MCP4922");
    return;
  }

  // Configure ADC1 Continuous Mode (DMA)
  // Define a configuração inicial de 43.200 Hz e o tamanho de um
  // "Frame" (quadro) é de 3 conversões. Com isso, o hardware do ADC fica
  // rodando sozinho em background. Ele lê R, depois S, depois T. Quando ele
  // junta essas 3 leituras, ele fecha 1 Frame.
  adc_continuous_handle_cfg_t adc_config = {
      // Multiplo exato do frame de 12 bytes: 240/12=20 frames de folga.
      .max_store_buf_size = 240,
      .conv_frame_size = 3 * SOC_ADC_DIGI_RESULT_BYTES, // 1 frame = 3 channels
  };
  ESP_ERROR_CHECK(adc_continuous_new_handle(&adc_config, &adc_handle));

  adc_continuous_config_t dig_cfg = {
      // Initial value required by the high-level driver. The exact 43.2 kHz
      // setting is applied from the ADC registers after the controller starts.
      .sample_freq_hz = ADC_TARGET_CONVERSION_FREQ_HZ,
      .conv_mode = ADC_CONV_SINGLE_UNIT_1,
  };

  adc_digi_pattern_config_t adc_pattern[3] = {0};
  // Define a configuração de cada canal do ADC
  adc_pattern[0].atten = ADC_ATTENUATION; // Faixa de tensão de entrada
  adc_pattern[0].channel = ADC_CHAN_R;    // Canal conectado ao ADC
  adc_pattern[0].unit = ADC_UNIT_1;       // ADC1 (pode ser 0 ou 1)
  adc_pattern[0].bit_width = SOC_ADC_DIGI_MAX_BITWIDTH; // Resolução: 12 bits

  adc_pattern[1].atten = ADC_ATTENUATION;
  adc_pattern[1].channel = ADC_CHAN_S;
  adc_pattern[1].unit = ADC_UNIT_1;
  adc_pattern[1].bit_width = SOC_ADC_DIGI_MAX_BITWIDTH;

  adc_pattern[2].atten = ADC_ATTENUATION;
  adc_pattern[2].channel = ADC_CHAN_T;
  adc_pattern[2].unit = ADC_UNIT_1;
  adc_pattern[2].bit_width = SOC_ADC_DIGI_MAX_BITWIDTH;

  dig_cfg.pattern_num = 3;
  dig_cfg.adc_pattern = adc_pattern;
  ESP_ERROR_CHECK(adc_continuous_config(adc_handle, &dig_cfg));

  // Register DMA Callback
  adc_continuous_evt_cbs_t cbs = {
      // Função chamada quando um frame completo do A/D estiver no buffer
      .on_conv_done = adc_on_conv_done, 
      // Função chamada quando o buffer do A/D estiver cheio
      .on_pool_ovf = adc_on_pool_overflow, 
  };
  ESP_ERROR_CHECK(
      adc_continuous_register_event_callbacks(adc_handle, &cbs, NULL));

  // Lê os dados de calibração na flash, se houver. Caso contrário, devolve
  // false e usa a calibração padrão. (A calibração padrão é usada apenas em
  // versões antigas do ESP32. Para o ESP32-S3 o dados de calibração sempre
  // estarão lá.)
  do_calibration =
      init_adc_calibration(ADC_UNIT_1, ADC_ATTENUATION, &adc_cali_handle);

  // Popula o LUT de calibração para acesso em O(1) sem Mutex
  ESP_LOGI(TAG, "Pre-computing ADC Calibration LUT...");
  for (int i = 0; i < 4096; i++) {
    int mv = 0;
    if (do_calibration) {
      adc_cali_raw_to_voltage(adc_cali_handle, i, &mv);
    } else {
      mv = (i * 3300) / 4095;
    }
    adc_cali_lut[i] = mv / 1000.0f; // Armazena diretamente em Volts
  }

  // Create Tasks
  xTaskCreatePinnedToCore(rms_calc_task, "rms_calc", 8192, NULL, 5,
                          &rms_task_handle, 0); // Core 0
  // Task apenas para retornar estatísticas sobre a conclusão das tarefas
  xTaskCreatePinnedToCore(control_stats_task, "control_stats", 4096, NULL, 4,
                          NULL, 0); // Core 0
  xTaskCreatePinnedToCore(control_loop_task, "control_loop", 8192, NULL,
                          configMAX_PRIORITIES - 1, &control_task_handle,
                          1); // Core 1
}
