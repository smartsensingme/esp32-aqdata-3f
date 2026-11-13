#include "app_control.h"
#include "app_diagnostics.h"
#include "app_fir.h"
#include "app_offsets.h"
#include "app_polarity.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_continuous.h"
#include "esp_clk_tree.h"
#include "esp_heap_caps.h"
#include "esp_intr_alloc.h"
#include "esp_log.h"
#include "esp_mcp4922.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/apb_saradc_struct.h"
#include "soc/clk_tree_defs.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>


static const char *TAG = "main";

#define NUM_CHIPS 2

// ADC Channels (using ADC1 on ESP32-S3: GPIO4, GPIO5, GPIO6)
#define ADC_CHAN_R ADC_CHANNEL_3
#define ADC_CHAN_S ADC_CHANNEL_4
#define ADC_CHAN_T ADC_CHANNEL_5

static mcp4922_context_t dac_ctx;

static TaskHandle_t control_task_handle = NULL;

// ADC Handles
static adc_continuous_handle_t adc_handle = NULL;
static adc_cali_handle_t adc_cali_handle = NULL;
static bool do_calibration = false;

// Calibration Lookup Table (16KB) to bypass FreeRTOS mutexes during 14.4kHz
// loop
static float adc_cali_lut[4096];
// Initialized once before starting DMA; read only by the control task.
static float phase_offsets[3];
static float inverse_fir_dc_gain;
// Calibration owner and ADC ISR are both on Core 1.
static volatile bool offset_dma_active, offset_dma_overflow;

// 14.4 kHz per phase gives exactly 240 samples per 60 Hz cycle. With the
// R/S/T pattern, the ADC must perform 43.2 k conversions per second.
#define ADC_CONVERSIONS_PER_FRAME 3U
#define ADC_TARGET_FRAME_FREQ_HZ ((float)APP_CONTROL_FRAME_RATE_HZ)
#define ADC_TARGET_CONVERSION_FREQ_HZ                                          \
  ((uint32_t)(ADC_TARGET_FRAME_FREQ_HZ * ADC_CONVERSIONS_PER_FRAME))

// Startup polarity check takes precedence over TEST_IS_ON and offset
// calibration.
#define CALIB_IS_ON 0
#define CALIB_DAC_STEP_CODES 128U

#if CALIB_IS_ON != 0 && CALIB_IS_ON != 1
#error "CALIB_IS_ON must be either 0 or 1"
#endif

_Static_assert(CALIB_DAC_STEP_CODES > 0 &&
                   CALIB_DAC_STEP_CODES <= DAC_MAX_CODE - DAC_NEUTRAL_CODE,
               "Polarity step must fit above the DAC midpoint");

// Se a malha de controle estiver fechada, faça TEST_IS_ON = 0
#define TEST_IS_ON 0

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
    const float angle =
        (2.0f * (float)M_PI * (float)i) / (float)TEST_SINE_POINTS;
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
  if (!offset_dma_active)
    app_diagnostics_frame_generated();
  // Sempre que o hardware fecha 1 Frame de 3 leituras, ele dispara
  // automaticamente a interrupção adc_on_conv_done. Como 43.200 / 3 = 14.400,
  // essa interrupção é chamada cravada a 14.4 kHz. O único trabalho dela é dar
  // um "cutucão" (Notify) na sua Task de controle.
  vTaskNotifyGiveFromISR(control_task_handle, &high_task_awoken);
  return (high_task_awoken == pdTRUE);
}

static bool IRAM_ATTR
adc_on_pool_overflow(adc_continuous_handle_t handle,
                     const adc_continuous_evt_data_t *edata, void *user_data) {
  (void)handle;
  (void)edata;
  (void)user_data;
  if (offset_dma_active)
    offset_dma_overflow = true;
  else
    app_diagnostics_pool_overflow();
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

/* Run from the pinned peripheral owner, before enabling acquisition. GDMA
 * allocates its interrupt during adc_continuous_new_handle(), not start(). */
static void init_adc_continuous(void) {
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

  ESP_LOGI(TAG, "ADC driver initialized on Core %d", xPortGetCoreID());
}

/* Startup calibration uses the same DMA pattern, Core 1 frame notifications,
 * LUT, FOH/FIR/controller workload and neutral SPI writes as operation. ADC is
 * stopped before sorting/NVS writes. No calibration code runs in the ISR. */
static esp_err_t calibrate_offsets_dma(void) {
  float (*samples)[APP_OFFSET_SAMPLE_COUNT] =
      heap_caps_malloc(6 * APP_OFFSET_SAMPLE_COUNT * sizeof(float),
                       MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!samples)
    return ESP_ERR_NO_MEM;
  float (*raw_samples)[APP_OFFSET_SAMPLE_COUNT] = samples + 3;
  uint16_t neutral[4] = {DAC_NEUTRAL_CODE, DAC_NEUTRAL_CODE, DAC_NEUTRAL_CODE,
                         DAC_NEUTRAL_CODE};
  const unsigned discard_frames =
      144000U; // 10 s at operating workload before zero capture
  float previous_r = 0, previous_t = 0;
  bool dac_failed = false;
  offset_dma_overflow = false;
  offset_dma_active = true;
  ESP_LOGW(TAG,
           "Calibrating offsets with operating DMA path: 10 s settle + 100 ms "
           "samples; mask=%u",
           7U);
  esp_err_t result = adc_continuous_start(adc_handle);
  if (result != ESP_OK) {
    offset_dma_active = false;
    free(samples);
    return result;
  }
  adc_apply_target_frame_rate();
  for (unsigned frame = 0; frame < discard_frames + APP_OFFSET_SAMPLE_COUNT;
       ++frame) {
    const unsigned pending = ulTaskNotifyTake(pdFALSE, pdMS_TO_TICKS(100));
    if (pending == 0 || pending > 4 || offset_dma_overflow) {
      result = ESP_ERR_INVALID_RESPONSE;
      break;
    }
    uint8_t buffer[12];
    uint32_t collected = 0;
    while (collected < sizeof(buffer)) {
      uint32_t got = 0;
      result = adc_continuous_read(adc_handle, buffer + collected,
                                   sizeof(buffer) - collected, &got, 0);
      if (result != ESP_OK || got == 0 || got > sizeof(buffer) - collected) {
        result = ESP_ERR_INVALID_RESPONSE;
        break;
      }
      collected += got;
    }
    if (result != ESP_OK)
      break;
    float volts[3] = {0};
    unsigned seen = 0;
    for (unsigned i = 0; i < sizeof(buffer); i += SOC_ADC_DIGI_RESULT_BYTES) {
      const adc_digi_output_data_t *sample =
          (const adc_digi_output_data_t *)&buffer[i];
      const unsigned ch = sample->type2.channel;
      const unsigned raw = sample->type2.data;
      if (ch < ADC_CHAN_R || ch > ADC_CHAN_T ||
          (seen & (1U << (ch - ADC_CHAN_R)))) {
        result = ESP_ERR_INVALID_RESPONSE;
        break;
      }
      const unsigned phase = ch - ADC_CHAN_R;
      seen |= 1U << phase;
      volts[phase] = adc_cali_lut[raw];
      if (raw == 0 || raw == 4095) {
        result = ESP_ERR_INVALID_RESPONSE;
        break;
      }
    }
    if (result != ESP_OK || seen != 7) {
      result = ESP_ERR_INVALID_RESPONSE;
      break;
    }

    const float r = volts[0] - phase_offsets[0],
                s = volts[1] - phase_offsets[1],
                t = volts[2] - phase_offsets[2];
    const float aligned[3] = {r + (r - previous_r) / 3.0f, s,
                              t - (t - previous_t) / 3.0f};
    previous_r = r;
    previous_t = t;
    esp_fir_t *firs[3] = {&fir_fase_r, &fir_fase_s, &fir_fase_t};
    app_parallel_t *controllers[3] = {&pr_fase_r, &pr_fase_s, &pr_fase_t};
    for (unsigned phase = 0; phase < 3; ++phase) {
      const float filtered =
          esp_fir_process_symmetric(firs[phase], aligned[phase]);
      if (frame >= discard_frames) {
        samples[phase][frame - discard_frames] =
            phase_offsets[phase] + filtered * inverse_fir_dc_gain;
        raw_samples[phase][frame - discard_frames] = volts[phase];
      }
      (void)app_control_process_phase(controllers[phase], filtered);
    }
    // Computation above maintains the normal workload; its output is discarded.
    result = mcp4922_ll_write_channels(&dac_ctx, neutral);
    if (result != ESP_OK) {
      dac_failed = true;
      break;
    }
  }
  const esp_err_t stop = adc_continuous_stop(adc_handle);
  offset_dma_active = false;
  if (result == ESP_OK)
    result = stop;
  (void)ulTaskNotifyTake(pdTRUE, 0);
  if (result == ESP_OK)
    result = adc_continuous_flush_pool(adc_handle);
  if (result == ESP_OK && offset_dma_overflow)
    result = ESP_ERR_INVALID_RESPONSE;
  if (result == ESP_OK)
    result =
        app_offsets_commit_calibration(raw_samples, samples, phase_offsets);
  free(samples);
  if (dac_failed)
    ESP_LOGE(TAG, "Calibration DAC fault; context retained, reset required");
  if (result == ESP_OK) {
    app_control_init();
    app_fir_init();
  }
  return result;
}

static void IRAM_ATTR control_loop_task(void *arg) {
  ESP_LOGI(TAG, "Entering DMA Control Loop on Core %d", xPortGetCoreID());
  init_adc_continuous();

  // Acquire bus permanently for ultra-fast writing
  // O ESP32-S3 possui dois barramentos SPI. Para o laço de controle não ter que
  // pedir acesso ao barramento a cada interrupção, nós adquirimos um barramento
  // aqui e não o liberamos mais. Qualquer task extra vai ter que usar o
  // barramento SPI.
  ESP_ERROR_CHECK(spi_device_acquire_bus(dac_ctx.spi_handle, portMAX_DELAY));
  uint16_t val[NUM_CHIPS * 2];
  for (unsigned i = 0; i < NUM_CHIPS * 2; ++i) {
    val[i] = app_control_voltage_to_dac(0.0f);
  }

  // Send a dummy transaction using the high-level API to properly configure the
  // SPI hardware
  ESP_ERROR_CHECK(mcp4922_write_channels(&dac_ctx, val));

  if (app_offsets_calibration_requested()) {
    const esp_err_t calibration = calibrate_offsets_dma();
    if (calibration != ESP_OK) {
      ESP_LOGE(TAG, "DMA calibration failed: %s; control stopped",
               esp_err_to_name(calibration));
      vTaskSuspend(NULL);
    }
  }

  // Phase Skew Compensation variables (First-Order Hold)
  static float prev_err_r = 0.0f;
  static float prev_err_t = 0.0f;

#if TEST_IS_ON
  uint16_t test_val[NUM_CHIPS * 2] = {0};
  uint32_t test_sine_index = 0;
#endif

  /*
   * Inicie o ADC somente depois que todo o estado do controle estiver pronto.
   * adc_continuous_start() reprograma os registradores, portanto a cadencia
   * exata ainda precisa ser aplicada logo depois dele. Nao deve haver logs ou
   * inicializacao adicional entre esse ponto e a espera pelo primeiro frame.
   */
  app_diagnostics_start();
  ESP_ERROR_CHECK(adc_continuous_start(adc_handle));
  adc_apply_target_frame_rate();

  while (1) {
    /*
     * Consume somente um evento. Se mais frames chegaram enquanto a task
     * estava ocupada, o contador permanece positivo e a proxima iteracao
     * comeca imediatamente, preservando a relacao 1:1 entre notificacao,
     * frame ADC e atualizacao do controle.
     */
#if CONFIG_ESP_RT_DIAGNOSTICS_ENABLE
    // Continue reporting empty windows if the ADC stops delivering frames.
    const uint32_t pending_frames = ulTaskNotifyTake(
        pdFALSE, pdMS_TO_TICKS(CONFIG_ESP_RT_DIAGNOSTICS_WINDOW_MS));
    if (pending_frames == 0) {
      app_diagnostics_idle();
      continue;
    }
#else
    const uint32_t pending_frames = ulTaskNotifyTake(pdFALSE, portMAX_DELAY);
#endif
    app_diagnostics_cycle_begin(pending_frames);

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
      const uint32_t bytes_remaining = sizeof(result_buf) - bytes_collected;
      esp_err_t ret =
          adc_continuous_read(adc_handle, result_buf + bytes_collected,
                              bytes_remaining, &ret_num, 0);

      if (ret != ESP_OK || ret_num == 0 || ret_num > bytes_remaining) {
        read_failed = true;
        break;
      }

      if (ret_num < bytes_remaining) {
        app_diagnostics_event(APP_DIAG_PARTIAL_READS);
      }
      bytes_collected += ret_num;
    }

    if (read_failed) {
      app_diagnostics_event(APP_DIAG_READ_ERRORS);
      app_diagnostics_cycle_end(false);
      continue;
    }

    app_diagnostics_stage_end(APP_DIAG_ADC);

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
    float err_r = adc_cali_lut[raw_r] - phase_offsets[0];
    float err_s = adc_cali_lut[raw_s] - phase_offsets[1];
    float err_t = adc_cali_lut[raw_t] - phase_offsets[2];

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
    float err_r_filtered =
        esp_fir_process_symmetric(&fir_fase_r, err_r_aligned);
    float err_s_filtered =
        esp_fir_process_symmetric(&fir_fase_s, err_s_aligned);
    float err_t_filtered =
        esp_fir_process_symmetric(&fir_fase_t, err_t_aligned);

    // 5. esp_pr: estados independentes por fase, saturacao e anti-windup.
    // app_control_process_phase chama esp_anti_windup_update e trata falhas
    // numericas.
    float control_action_r =
        app_control_process_phase(&pr_fase_r, err_r_filtered);
    float control_action_s =
        app_control_process_phase(&pr_fase_s, err_s_filtered);
    float control_action_t =
        app_control_process_phase(&pr_fase_t, err_t_filtered);

    // 6. Map [-6 V, +6 V] to the unsigned 12-bit DAC range.
    val[0] = app_control_voltage_to_dac(control_action_r);
    val[1] = app_control_voltage_to_dac(control_action_s);
    val[2] = app_control_voltage_to_dac(control_action_t);
    val[3] = DAC_NEUTRAL_CODE;

    app_diagnostics_stage_end(APP_DIAG_COMPUTE);

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
    test_val[3] = DAC_NEUTRAL_CODE;
    esp_err_t dac_result = mcp4922_ll_write_channels(&dac_ctx, test_val);

    test_sine_index++;
    if (test_sine_index >= TEST_SINE_POINTS) {
      test_sine_index = 0;
    }
#else
    esp_err_t dac_result = mcp4922_ll_write_channels(&dac_ctx, val);
#endif

    if (dac_result != ESP_OK) {
      ESP_ERROR_CHECK(adc_continuous_stop(adc_handle));
      app_diagnostics_cycle_end(false);
      /* A failed transaction may still be active. Retain the context and bus
       * reservation, stop acquisition and require an explicit system reset. */
      ESP_LOGE(TAG, "DAC fault: %s; acquisition stopped, reset required",
               esp_err_to_name(dac_result));
      vTaskSuspend(NULL);
    }
    app_diagnostics_stage_end(APP_DIAG_DAC);

    // Telemetry records the codes actually sent to the DAC.
    /* Publish READY atomically; the Core 0 worker polls without a kernel
     * notification from this timing-sensitive path. */
    const float adc_volts[3] = {adc_cali_lut[raw_r], adc_cali_lut[raw_s],
                                adc_cali_lut[raw_t]};
    const float filtered_volts[3] = {
        phase_offsets[0] + err_r_filtered * inverse_fir_dc_gain,
        phase_offsets[1] + err_s_filtered * inverse_fir_dc_gain,
        phase_offsets[2] + err_t_filtered * inverse_fir_dc_gain};
#if TEST_IS_ON
    (void)app_control_store_sample(test_val, adc_volts, filtered_volts);
#else
    (void)app_control_store_sample(val, adc_volts, filtered_volts);
#endif

    app_diagnostics_cycle_end(true);
  }
}

/* Continuous mode only: runs once from the Core 0 reporter with ADC active. */
static void report_adc_rate(void) {
  float rate = 0.0f;
  if (adc_get_configured_conversion_freq(&rate)) {
    ESP_LOGI(TAG, "ADC configurado: %.3f Hz por conversao, %.3f Hz por frame",
             (double)rate, (double)(rate / ADC_CONVERSIONS_PER_FRAME));
  } else {
    ESP_LOGW(TAG, "Could not estimate ADC sampling rate from registers");
  }
}

static void rms_calc_task(void *arg) {
  TickType_t next_report = xTaskGetTickCount();
  unsigned report_number = 0;
  while (1) {
    vTaskDelayUntil(&next_report, pdMS_TO_TICKS(10000));

    if (!app_control_request_capture()) {
      ESP_LOGE(TAG,
               "RMS capture request rejected: previous capture still owned");
      continue;
    }

    float rms_r = 0.0f, rms_s = 0.0f, rms_t = 0.0f;
    /* A failed read only checks READY, leaving the output/buffer untouched.
     * Sleep one tick (10 ms at 100 Hz), preserving CPU time for Core 0 Idle.
     * Polling adds up to one nominal tick, plus scheduling latency. */
    while (!app_control_calc_rms_report(&rms_r, &rms_s, &rms_t,
                                        (report_number + 1) % 6 == 0)) {
      vTaskDelay(1);
    }

    ++report_number;
  }
}

void app_main(void) {
  ESP_LOGI(TAG, "Starting Digital Control Loop Architecture (ESP32-S3)");

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

  // Hold every output at bipolar zero throughout startup and calibration.
  uint16_t startup_values[NUM_CHIPS * 2];
  for (unsigned i = 0; i < NUM_CHIPS * 2; ++i) {
    startup_values[i] = app_control_voltage_to_dac(0.0f);
  }
  ESP_ERROR_CHECK(mcp4922_write_channels(&dac_ctx, startup_values));

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

#if CALIB_IS_ON
  app_polarity_run(&dac_ctx, adc_cali_lut, CALIB_DAC_STEP_CODES);
  return;
#endif


  app_control_init();
  ESP_LOGI(TAG,
           "Control gains: PR Kp=%.3g Kr60=%.3g; PID Kp=0 Ki=%.3g Kd=0; "
           "phases=R/S/T; normal limit=%.3f Vsoft",
           (double)APP_CONTROL_KP, (double)APP_CONTROL_KR60,
           (double)APP_CONTROL_KI,
           (double)(CONFIG_APP_CONTROL_LIMIT_MV / 1000.0f));
  app_fir_init();
  inverse_fir_dc_gain = 1.0f / app_fir_dc_gain();
#if TEST_IS_ON
  test_sine_lut_init();
  ESP_LOGW(TAG,
           "DAC test mode enabled: outputting three-phase 60 Hz sine waves");
#endif

  // All flash access and temporary oneshot ownership end before DMA starts.
  ESP_ERROR_CHECK(app_offsets_init(adc_cali_lut, phase_offsets));

  // Create Tasks
  xTaskCreatePinnedToCore(rms_calc_task, "rms_calc", 8192, NULL, 5, NULL,
                          0);            // Core 0
  app_diagnostics_init(report_adc_rate); // Reporter proprio no Core 0
  xTaskCreatePinnedToCore(control_loop_task, "control_loop", 8192, NULL,
                          configMAX_PRIORITIES - 1, &control_task_handle,
                          1); // Core 1
}
