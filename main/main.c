#include "app_control.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_continuous.h"
#include "esp_log.h"
#include "esp_mcp4922.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>
#include <stdio.h>

#include "app_mqtt.h"
#include "esp_timer.h"

static const char *TAG = "main";

#define NUM_CHIPS 2

// ADC Channels (using ADC1 on ESP32-S3: GPIO4, GPIO5, GPIO6)
#define ADC_CHAN_R ADC_CHANNEL_3
#define ADC_CHAN_S ADC_CHANNEL_4
#define ADC_CHAN_T ADC_CHANNEL_5

static mcp4922_context_t dac_ctx;

static TaskHandle_t control_task_handle = NULL;
static TaskHandle_t rms_task_handle = NULL;


// ADC Handles
static adc_continuous_handle_t adc_handle = NULL;
static adc_cali_handle_t adc_cali_handle = NULL;
static bool do_calibration = false;

// Calibration Lookup Table (16KB) to bypass FreeRTOS mutexes during 13.5kHz
// loop
static float adc_cali_lut[4096];

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
  // Sempre que o hardware fecha 1 Frame de 3 leituras, ele dispara
  // automaticamente a interrupção adc_on_conv_done. Como 40.500 / 3 = 13.500,
  // essa interrupção é chamada cravada a 13.5 kHz. O único trabalho dela é dar
  // um "cutucão" (Notify) na sua Task de controle.
  vTaskNotifyGiveFromISR(control_task_handle, &high_task_awoken);
  return (high_task_awoken == pdTRUE);
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

  // Start DMA conversions
  ESP_ERROR_CHECK(adc_continuous_start(adc_handle));

  // Pre-load pointers and variables into local stack/registers to prevent
  // cache-misses during the critical IRQ-like loop
  uint16_t *lut = sine_lut;
  static uint32_t phase_acc = 0;

  // DDS Phase variables (auto-calibrated later)
  static uint32_t phase_inc = 18775813;  // default for ~13725 Hz
  const uint32_t phase_120 = 1431655765; // (2^32) / 3
  const uint32_t phase_240 = 2863311530; // (2^32) * 2 / 3

  // Auto-calibration variables
  // Necessário para calcular a freq. de amostragem exata (real_f_loop)
  static uint64_t calib_start_time = 0;
  static uint32_t calib_sample_count = 0;
  static bool dds_calibrated = false;

  // Phase Skew Compensation variables (First-Order Hold)
  static float prev_err_r = 0.0f;
  static float prev_err_t = 0.0f;

  while (1) {
    // Wait for notification from DMA ISR
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    // 1. Read RAW values from ADC DMA Buffer
    uint8_t result_buf[12] = {0}; // 3 channels * 4 bytes
    uint32_t ret_num = 0;

    // We fetch the DMA block of 3 readings
    esp_err_t ret = adc_continuous_read(adc_handle, result_buf,
                                        sizeof(result_buf), &ret_num, 0);
    if (ret != ESP_OK || ret_num == 0) {
      continue;
    }

    int raw_r = 0, raw_s = 0, raw_t = 0;

    // Parse the DMA frame (ESP32-S3 uses type2 format)
    for (int i = 0; i < ret_num; i += SOC_ADC_DIGI_RESULT_BYTES) {
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

    // 4. Proportional-Resonant (PR) Controller
    // Passamos os erros alinhados espacialmente no tempo exato da Fase S
    // (pr_fase_x é definido no app_control.c e armazena os valosres passados
    // de erro e ação de controle)
    float control_action_r = calcular_controle_PR(&pr_fase_r, err_r_aligned);
    float control_action_s = calcular_controle_PR(&pr_fase_s, err_s_aligned);
    float control_action_t = calcular_controle_PR(&pr_fase_t, err_t_aligned);

    // 5. Real Actuation: Arredondamento e Casting rápido
    val[0] = (uint16_t)(control_action_r + 0.5f);
    val[1] = (uint16_t)(control_action_s + 0.5f);
    val[2] = (uint16_t)(control_action_t + 0.5f);
    val[3] = 0;

    // Dummy data for sine waves
    // 5. Temporary Actuation: Write Sine Waves to DAC (Bare-Metal)
    // --- START OF CRITICAL DETERMINISTIC REGION ---

    // 2. Control Math / Sine LUT (DDS Phase Accumulator)
    // SINE_POINTS is 1024 (2^10), so we shift by 32 - 10 = 22
    // A mágica do >> 22 é pegar esse número astronômico (2^32) e "espremê-lo"
    // de volta para um índice pequeno de 0 a 1023 (10 bits) para ler a tabela
    // na memória instantaneamente, sem usar divisão ou matemática pesada de
    // ponto flutuante dentro do laço crítico.
    int idx_r = phase_acc >> 22;
    int idx_s = (phase_acc + phase_120) >> 22;
    int idx_t = (phase_acc + phase_240) >> 22;

    val[0] = lut[idx_r]; // Fase R (0 graus)
    val[1] = lut[idx_s]; // Fase S (120 graus)
    val[2] = lut[idx_t]; // Fase T (240 graus)
    val[3] = 0;          // Unused channel

    // 3. Write to DAC using ultra-fast bare-metal driver
    mcp4922_ll_write_channels(&dac_ctx, val);

    // 4. Update phase accumulator for the next cycle
    phase_acc += phase_inc;

    // 5. DDS Auto-Calibration (measures exact hardware interrupt frequency)
    if (!dds_calibrated) {
      if (calib_sample_count == 0) {
        calib_start_time = esp_timer_get_time();
      }
      calib_sample_count++;

      if (calib_sample_count == 20000) { // ~1.5 segundos de medição
        uint64_t calib_end_time = esp_timer_get_time();
        float elapsed_sec = (calib_end_time - calib_start_time) / 1000000.0f;
        float real_f_loop = 20000.0f / elapsed_sec;

        // Ajusta o phase_inc com a taxa exata medida no hardware!
        phase_inc = (uint32_t)((60.0f * 4294967296.0f) / real_f_loop);

        ESP_LOGI(TAG, "--- DDS CALIBRADO ---");
        ESP_LOGI(TAG, "Tempo para 20000 amostras: %.4f s", elapsed_sec);
        ESP_LOGI(TAG, "Freq de Amostragem Real (F_loop): %.2f Hz", real_f_loop);
        ESP_LOGI(TAG, "Novo Phase Increment: %lu", phase_inc);
        ESP_LOGI(TAG, "Onda ajustada para EXATOS 60.00 Hz!");

        dds_calibrated = true;
      }
    }

    // --- END OF CRITICAL DETERMINISTIC REGION ---

    if (app_control_store_telemetry(val[0], val[1], val[2])) {
      xTaskNotifyGive(rms_task_handle);
    }
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
  // Define frequência de amostragem total para 41175 Hz e o tamanho de um
  // "Frame" (quadro) é de 3 conversões. Com isso, o hardware do ADC fica
  // rodando sozinho em background. Ele lê R, depois S, depois T. Quando ele
  // junta essas 3 leituras, ele fecha 1 Frame.
  adc_continuous_handle_cfg_t adc_config = {
      .max_store_buf_size = 1024,
      .conv_frame_size = 3 * SOC_ADC_DIGI_RESULT_BYTES, // 1 frame = 3 channels
  };
  ESP_ERROR_CHECK(adc_continuous_new_handle(&adc_config, &adc_handle));

  adc_continuous_config_t dig_cfg = {
      .sample_freq_hz =
          40500, // IMPORTANTE: O hardware não consegue gerar 40500 Hz. Ele
                 // arredonda para 41175 Hz (13.725 kHz por canal). O DDS
                 // usa 13.725 kHz para cravar 60 Hz.
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
      .on_conv_done = adc_on_conv_done, // Função que será chamada quando
                                        // um frame for completo
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
  xTaskCreatePinnedToCore(control_loop_task, "control_loop", 8192, NULL,
                          configMAX_PRIORITIES - 1, &control_task_handle,
                          1); // Core 1
}
