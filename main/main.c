#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_continuous.h"
#include "esp_log.h"
#include "esp_mcp4922.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>
#include <stdio.h>

static const char *TAG = "main";

#define NUM_CHIPS 2
#define SINE_POINTS 225

// Constants for conditioning
#define OFFSET_R -2.9f
#define OFFSET_S -2.9f
#define OFFSET_T -2.9f

// Constants for Outgoing Control Effort (Telemetry)
#define RMS_BUFFER_SIZE 675 // 3 ciclos de 60Hz

// Ganho do amplificador de condicionamento do sinal do DAC
#define OUT_GAIN 1.0f
// Offset do amplificador de condicionamento do sinal do DAC
#define OUT_OFFSET -1.65f
// Cargar (Ohms) que transforma a tensão do condicionador em corrente.
#define R_LOAD 10.0f

// ADC Channels (using ADC1 on ESP32-S3: GPIO4, GPIO5, GPIO6)
#define ADC_CHAN_R ADC_CHANNEL_3
#define ADC_CHAN_S ADC_CHANNEL_4
#define ADC_CHAN_T ADC_CHANNEL_5

// PR Controller Constants (Calculated via Octave for Bits Domain)
#define PR_B0 12.532f
#define PR_B1 -24.115f
#define PR_B2 11.601f
#define PR_A1 -1.995f
#define PR_A2 0.998f
#define PR_OFFSET -1.650f

typedef struct {
  float e[3]; // Histórico de erros: [0]=atual, [1]=k-1, [2]=k-2
  float u[3]; // Histórico de ações: [0]=atual, [1]=k-1, [2]=k-2
} PR_Estado_t;

static PR_Estado_t pr_fase_r, pr_fase_s, pr_fase_t;

////////////////////////////////////////////////////////////////////////////////
// O que segue são dummy data usados para gerar um sinal senoidal trifásico
// para testar se o loop de controle está determinístico.
// The LUT for 60Hz sine at 13.5kHz sampling rate. Placed in RAM.
DRAM_ATTR static uint16_t sine_lut[SINE_POINTS];

// Indices for phase shifting (3-phase network)
#define PHASE_0_OFFSET 0
#define PHASE_120_OFFSET 75  // 120 degrees = 1/3 of 225 points
#define PHASE_240_OFFSET 150 // 240 degrees = 2/3 of 225 points
// Fim do dummy data
////////////////////////////////////////////////////////////////////////////////

static mcp4922_context_t dac_ctx;

static TaskHandle_t control_task_handle = NULL;
static TaskHandle_t rms_task_handle = NULL;

// Telemetry
// Para medição do valor RMS da corrente a ser medida
static volatile bool capture_requested = false;
static volatile int capture_idx = 0;
static uint16_t dac_history_r[RMS_BUFFER_SIZE];
static uint16_t dac_history_s[RMS_BUFFER_SIZE];
static uint16_t dac_history_t[RMS_BUFFER_SIZE];

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

static void pr_init(PR_Estado_t *estado) {
  for (int i = 0; i < 3; i++) {
    estado->e[i] = 0.0f;
    estado->u[i] = 0.0f;
  }
}

static inline float IRAM_ATTR calcular_controle_PR(PR_Estado_t *estado,
                                                   float erro_atual) {
  estado->e[0] = erro_atual;

  estado->u[0] = (PR_B0 * estado->e[0]) + (PR_B1 * estado->e[1]) +
                 (PR_B2 * estado->e[2]) - (PR_A1 * estado->u[1]) -
                 (PR_A2 * estado->u[2]) + PR_OFFSET;

  if (estado->u[0] > 4095.0f)
    estado->u[0] = 4095.0f;
  if (estado->u[0] < 0.0f)
    estado->u[0] = 0.0f;

  estado->e[2] = estado->e[1];
  estado->e[1] = estado->e[0];
  estado->u[2] = estado->u[1];
  estado->u[1] = estado->u[0];

  return estado->u[0];
}

static void IRAM_ATTR control_loop_task(void *arg) {
  ESP_LOGI(TAG, "Entering DMA Control Loop on Core %d", xPortGetCoreID());

  pr_init(&pr_fase_r);
  pr_init(&pr_fase_s);
  pr_init(&pr_fase_t);

  // Acquire bus permanently for ultra-fast writing
  spi_device_acquire_bus(dac_ctx.spi_handle, portMAX_DELAY);
  uint16_t val[NUM_CHIPS * 2] = {0};

  // Send a dummy transaction using the high-level API to properly configure the
  // SPI hardware
  mcp4922_write_channels(&dac_ctx, val);

  // Start DMA conversions
  ESP_ERROR_CHECK(adc_continuous_start(adc_handle));

  // Pre-load pointers and variables into local stack/registers to prevent
  // literal pool (.rodata) fetches during the fast-path loop.
  mcp4922_context_t *ctx = &dac_ctx;
  uint16_t *lut = sine_lut;
  int local_lut_idx = 0; // Para dummy data apenas

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

    // 4. Proportional-Resonant (PR) Controller
    float control_action_r = calcular_controle_PR(&pr_fase_r, err_r);
    float control_action_s = calcular_controle_PR(&pr_fase_s, err_s);
    float control_action_t = calcular_controle_PR(&pr_fase_t, err_t);

    // 5. Real Actuation: Arredondamento e Casting rápido
    val[0] = (uint16_t)(control_action_r + 0.5f);
    val[1] = (uint16_t)(control_action_s + 0.5f);
    val[2] = (uint16_t)(control_action_t + 0.5f);
    val[3] = 0;

    // Dummy data for sine waves
    // 5. Temporary Actuation: Write Sine Waves to DAC (Bare-Metal)
    // (Will be replaced by control_action mappings later)
    int idx_s = local_lut_idx + PHASE_120_OFFSET;
    if (idx_s >= SINE_POINTS)
      idx_s -= SINE_POINTS;

    int idx_t = local_lut_idx + PHASE_240_OFFSET;
    if (idx_t >= SINE_POINTS)
      idx_t -= SINE_POINTS;

    val[0] = lut[local_lut_idx]; // Fase R (0 graus)
    val[1] = lut[idx_s];         // Fase S (120 graus)
    val[2] = lut[idx_t];         // Fase T (240 graus)
    val[3] = 0;                  // Unused channel
    // end of dummy data

    mcp4922_ll_write_channels(ctx, val);

    if (capture_requested) {
      dac_history_r[capture_idx] = val[0];
      dac_history_s[capture_idx] = val[1];
      dac_history_t[capture_idx] = val[2];
      capture_idx++;
      if (capture_idx >= RMS_BUFFER_SIZE) {
        capture_requested = false;
        xTaskNotifyGive(rms_task_handle);
      }
    }

    local_lut_idx++;
    if (local_lut_idx >= SINE_POINTS) {
      local_lut_idx = 0;
    }
  }
}

static void rms_calc_task(void *arg) {
  while (1) {
    vTaskDelay(pdMS_TO_TICKS(2000));

    capture_idx = 0;
    capture_requested = true;

    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    float sum_sq_r = 0.0f;
    float sum_sq_s = 0.0f;
    float sum_sq_t = 0.0f;

    for (int i = 0; i < RMS_BUFFER_SIZE; i++) {
      // R
      float v_dac_r = (dac_history_r[i] * 3.3f) / 4095.0f;
      float v_cond_r = (v_dac_r * OUT_GAIN) + OUT_OFFSET;
      float i_out_r = v_cond_r / R_LOAD;
      sum_sq_r += i_out_r * i_out_r;

      // S
      float v_dac_s = (dac_history_s[i] * 3.3f) / 4095.0f;
      float v_cond_s = (v_dac_s * OUT_GAIN) + OUT_OFFSET;
      float i_out_s = v_cond_s / R_LOAD;
      sum_sq_s += i_out_s * i_out_s;

      // T
      float v_dac_t = (dac_history_t[i] * 3.3f) / 4095.0f;
      float v_cond_t = (v_dac_t * OUT_GAIN) + OUT_OFFSET;
      float i_out_t = v_cond_t / R_LOAD;
      sum_sq_t += i_out_t * i_out_t;
    }

    float rms_r = sqrtf(sum_sq_r / RMS_BUFFER_SIZE);
    float rms_s = sqrtf(sum_sq_s / RMS_BUFFER_SIZE);
    float rms_t = sqrtf(sum_sq_t / RMS_BUFFER_SIZE);

    ESP_LOGI(TAG, "Corrente RMS Fase R: %.3f A", rms_r);
    ESP_LOGI(TAG, "Corrente RMS Fase S: %.3f A", rms_s);
    ESP_LOGI(TAG, "Corrente RMS Fase T: %.3f A", rms_t);
  }
}

void app_main(void) {
  ESP_LOGI(TAG, "Starting Digital Control Loop Architecture (ESP32-S3)");

  // Populate the Sine LUT
  for (int i = 0; i < SINE_POINTS; i++) {
    double angle = (2.0 * M_PI * i) / SINE_POINTS;
    sine_lut[i] = (uint16_t)(2048.0 + 2047.0 * sin(angle));
  }

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
  // Define frequência de amostragem total para 40500 Hz e o tamanho de um
  // "Frame" (quadro) é de 3 conversões. Com isso, o hardware do ADC fica
  // rodando sozinho em background. Ele lê R, depois S, depois T. Quando ele
  // junta essas 3 leituras, ele fecha 1 Frame.
  adc_continuous_handle_cfg_t adc_config = {
      .max_store_buf_size = 1024,
      .conv_frame_size = 3 * SOC_ADC_DIGI_RESULT_BYTES, // 1 frame = 3 channels
  };
  ESP_ERROR_CHECK(adc_continuous_new_handle(&adc_config, &adc_handle));

  adc_continuous_config_t dig_cfg = {
      .sample_freq_hz = 40500, // 13.5 kHz * 3 channels
      .conv_mode = ADC_CONV_SINGLE_UNIT_1,
  };

  adc_digi_pattern_config_t adc_pattern[3] = {0};
  adc_pattern[0].atten = ADC_ATTENUATION;
  adc_pattern[0].channel = ADC_CHAN_R;
  adc_pattern[0].unit = ADC_UNIT_1;
  adc_pattern[0].bit_width = SOC_ADC_DIGI_MAX_BITWIDTH;

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
      .on_conv_done = adc_on_conv_done,
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
