#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 1
#define ESP_ERR_INVALID_ARG 2
#define ESP_ERR_INVALID_RESPONSE 3
#define ESP_ERR_NVS_NOT_FOUND 4
#define ESP_ERR_NVS_INVALID_LENGTH 5
#define CONFIG_APP_OFFSET_CAL_GPIO 7
#define CONFIG_MCP4922_CS1_PIN 10
#define CONFIG_MCP4922_CS2_PIN 9
#define CONFIG_MCP4922_MOSI_PIN 11
#define CONFIG_MCP4922_SCK_PIN 12
#define CONFIG_MCP4922_LDAC_PIN 14
#define GPIO_IS_VALID_GPIO(x) ((x) >= 0 && (x) < 49)
#define GPIO_MODE_INPUT 0
#define GPIO_PULLUP_ENABLE 1
#define GPIO_PULLDOWN_DISABLE 0
#define GPIO_INTR_DISABLE 0
typedef struct {
  uint64_t pin_bit_mask;
  int mode, pull_up_en, pull_down_en, intr_type;
} gpio_config_t;
esp_err_t gpio_config(const gpio_config_t *);
int gpio_get_level(int);
#define pdMS_TO_TICKS(x) (x)
void vTaskDelay(unsigned);
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
typedef int adc_channel_t;
typedef void *adc_oneshot_unit_handle_t;
#define ADC_CHANNEL_3 3
#define ADC_CHANNEL_4 4
#define ADC_CHANNEL_5 5
#define ADC_UNIT_1 0
#define ADC_ATTEN_DB_12 12
#define ADC_BITWIDTH_12 12
typedef struct {
  int unit_id;
} adc_oneshot_unit_init_cfg_t;
typedef struct {
  int atten, bitwidth;
} adc_oneshot_chan_cfg_t;
esp_err_t adc_oneshot_new_unit(const adc_oneshot_unit_init_cfg_t *,
                               adc_oneshot_unit_handle_t *);
esp_err_t adc_oneshot_config_channel(adc_oneshot_unit_handle_t, adc_channel_t,
                                     const adc_oneshot_chan_cfg_t *);
esp_err_t adc_oneshot_read(adc_oneshot_unit_handle_t, adc_channel_t, int *);
esp_err_t adc_oneshot_del_unit(adc_oneshot_unit_handle_t);
typedef int nvs_handle_t;
#define NVS_READWRITE 0
esp_err_t nvs_flash_init(void);
esp_err_t nvs_open(const char *, int, nvs_handle_t *);
esp_err_t nvs_get_blob(nvs_handle_t, const char *, void *, size_t *);
esp_err_t nvs_set_blob(nvs_handle_t, const char *, const void *, size_t);
esp_err_t nvs_commit(nvs_handle_t);
void nvs_close(nvs_handle_t);
