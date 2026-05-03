#include "esp_mcp4922.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal/gpio_hal.h"
#include "rom/ets_sys.h"
#include "soc/spi_struct.h"
#include <string.h>

static const char *TAG = "MCP4922";

#define MCP4922_DAC_A_BIT (0 << 15)
#define MCP4922_DAC_B_BIT (1 << 15)
#define MCP4922_BUF_BIT (1 << 14)
#define MCP4922_GAIN_1X (1 << 13)
#define MCP4922_GAIN_2X (0 << 13)
#define MCP4922_SHDN_BIT (1 << 12)

esp_err_t mcp4922_init(const mcp4922_config_t *config, mcp4922_context_t *ctx) {
  ESP_RETURN_ON_FALSE(config != NULL, ESP_ERR_INVALID_ARG, TAG,
                      "Config must not be NULL");
  ESP_RETURN_ON_FALSE(ctx != NULL, ESP_ERR_INVALID_ARG, TAG,
                      "Context must not be NULL");
  ESP_RETURN_ON_FALSE(config->num_chips > 0, ESP_ERR_INVALID_ARG, TAG,
                      "num_chips must be > 0");
  ESP_RETURN_ON_FALSE(config->cs_pins != NULL, ESP_ERR_INVALID_ARG, TAG,
                      "cs_pins array must not be NULL");

  // Initialize SPI bus
  spi_bus_config_t buscfg = {
      .miso_io_num = -1, // Not used
      .mosi_io_num = config->mosi_io_num,
      .sclk_io_num = config->sck_io_num,
      .quadwp_io_num = -1,
      .quadhd_io_num = -1,
      .max_transfer_sz = config->num_chips *
                         4 // Max transfer size needed for 2 channels per chip
                           // (16 bit each = 2 bytes * 2 = 4 bytes per chip)
  };

  // We only initialize the bus if it's not already initialized.
  // In ESP-IDF, if you call spi_bus_initialize on an already init'd bus, it
  // returns ESP_ERR_INVALID_STATE. We'll try to init, and ignore INVALID_STATE.
  // Use SPI_DMA_DISABLED (0) because all our transactions are 16-bits and DMA
  // setup overhead is too high.
  esp_err_t ret =
      spi_bus_initialize(config->host_id, &buscfg, 0); // 0 = SPI_DMA_DISABLED
  if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
    ESP_LOGE(TAG, "Failed to initialize SPI bus: %s", esp_err_to_name(ret));
    return ret;
  }

  // Set hardware pointers directly to avoid literal pool loads from Flash in IRAM
#if CONFIG_IDF_TARGET_ESP32
  ctx->hw = (void *)&SPI2;
#else
  ctx->hw = (void *)&GPSPI2;
#endif
  ctx->gpio_hw = (void *)&GPIO;

  // Set CS pins directly in the struct array
  ESP_RETURN_ON_FALSE(config->num_chips <= 4, ESP_ERR_INVALID_ARG, TAG,
                      "Too many chips for fixed array");
  ctx->num_chips = config->num_chips;
  ctx->ldac_io_num = config->ldac_io_num;

  // Pre-calculate configuration bits
  uint16_t base_config = MCP4922_SHDN_BIT; // Active mode
  if (config->vref_buffered) {
    base_config |= MCP4922_BUF_BIT;
  }
  if (!config->gain_2x) {
    base_config |= MCP4922_GAIN_1X;
  } else {
    base_config |= MCP4922_GAIN_2X;
  }

  ctx->config_bits_a = base_config | MCP4922_DAC_A_BIT;
  ctx->config_bits_b = base_config | MCP4922_DAC_B_BIT;

  // Add a single SPI device with software CS
  spi_device_interface_config_t devcfg = {
      .clock_speed_hz = 20 * 1000 * 1000, // 20 MHz max as per MCP4922 datasheet
      .mode = 0,                          // SPI mode 0 (CPOL=0, CPHA=0)
      .spics_io_num = -1, // Manual CS control for much lower overhead
      .queue_size = 2,
      .flags = SPI_DEVICE_HALFDUPLEX,
  };

  ret = spi_bus_add_device(config->host_id, &devcfg, &ctx->spi_handle);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to add SPI device: %s", esp_err_to_name(ret));
    return ret;
  }

  // Configure CS pins
  for (int i = 0; i < config->num_chips; i++) {
    ctx->cs_pins[i] = config->cs_pins[i];
    gpio_config_t io_conf = {.intr_type = GPIO_INTR_DISABLE,
                             .mode = GPIO_MODE_OUTPUT,
                             .pin_bit_mask = (1ULL << config->cs_pins[i]),
                             .pull_down_en = 0,
                             .pull_up_en = 0};
    gpio_config(&io_conf);
    gpio_set_level(config->cs_pins[i],
                   1); // CS is active low, keep high by default
  }

  // Configure LDAC pin
  if (config->ldac_io_num >= 0) {
    gpio_config_t io_conf = {.intr_type = GPIO_INTR_DISABLE,
                             .mode = GPIO_MODE_OUTPUT,
                             .pin_bit_mask = (1ULL << config->ldac_io_num),
                             .pull_down_en = 0,
                             .pull_up_en = 0};
    gpio_config(&io_conf);
    gpio_set_level(config->ldac_io_num,
                   1); // LDAC is active low, keep high by default
  }

  return ESP_OK;
}

esp_err_t mcp4922_write_channels(mcp4922_context_t *ctx,
                                 uint16_t *channel_values) {
  if (!ctx || !channel_values)
    return ESP_ERR_INVALID_ARG;

  for (int i = 0; i < ctx->num_chips; i++) {
    uint16_t val_a = channel_values[i * 2] & 0x0FFF;
    uint16_t val_b = channel_values[i * 2 + 1] & 0x0FFF;

    uint16_t data_a = ctx->config_bits_a | val_a;
    uint16_t data_b = ctx->config_bits_b | val_b;

    spi_transaction_t t_a = {
        .flags = SPI_TRANS_USE_TXDATA,
        .length = 16,
    };
    t_a.tx_data[0] = (data_a >> 8) & 0xFF;
    t_a.tx_data[1] = data_a & 0xFF;

    spi_transaction_t t_b = {
        .flags = SPI_TRANS_USE_TXDATA,
        .length = 16,
    };
    t_b.tx_data[0] = (data_b >> 8) & 0xFF;
    t_b.tx_data[1] = data_b & 0xFF;

    // Transmit Channel A
    gpio_set_level(ctx->cs_pins[i], 0);
    esp_err_t ret = spi_device_polling_transmit(ctx->spi_handle, &t_a);
    gpio_set_level(ctx->cs_pins[i], 1);
    if (ret != ESP_OK)
      return ret;

    // Transmit Channel B
    gpio_set_level(ctx->cs_pins[i], 0);
    ret = spi_device_polling_transmit(ctx->spi_handle, &t_b);
    gpio_set_level(ctx->cs_pins[i], 1);
    if (ret != ESP_OK)
      return ret;
  }

  // Latch data (LDAC pulse)
  if (ctx->ldac_io_num >= 0) {
    gpio_set_level(ctx->ldac_io_num, 0);
    ets_delay_us(1); // Small delay to satisfy LDAC pulse width req (min 100ns)
    gpio_set_level(ctx->ldac_io_num, 1);
  }

  return ESP_OK;
}

void IRAM_ATTR mcp4922_ll_write_channels(mcp4922_context_t *ctx,
                                         uint16_t *channel_values) {
  // Ultra-fast bare-metal SPI transmission.
  spi_dev_t *hw = (spi_dev_t *)ctx->hw;
  gpio_dev_t *gpio_hw = (gpio_dev_t *)ctx->gpio_hw;

  for (int i = 0; i < ctx->num_chips; i++) {
    uint16_t val_a = channel_values[i * 2] & 0x0FFF;
    uint16_t val_b = channel_values[i * 2 + 1] & 0x0FFF;

    uint16_t data_a = ctx->config_bits_a | val_a;
    uint16_t data_b = ctx->config_bits_b | val_b;

    // ESP32 SPI transmits from data_buf[0] starting from byte 0.
    // We need MSByte to go out first, so we swap the bytes.
    uint32_t word_a = __builtin_bswap16(data_a);
    uint32_t word_b = __builtin_bswap16(data_b);

    // Transmit Channel A
    gpio_ll_set_level(gpio_hw, ctx->cs_pins[i], 0);
    hw->data_buf[0] = word_a;
    hw->cmd.usr = 1;
    while (hw->cmd.usr)
      ; // wait for hardware transfer
    gpio_ll_set_level(gpio_hw, ctx->cs_pins[i], 1);

    // Satisfy CS high time between commands (15ns min for MCP4922)
    for (int n = 0; n < 5; n++)
      asm volatile("nop");

    // Transmit Channel B
    gpio_ll_set_level(gpio_hw, ctx->cs_pins[i], 0);
    hw->data_buf[0] = word_b;
    hw->cmd.usr = 1;
    while (hw->cmd.usr)
      ; // wait for hardware transfer
    gpio_ll_set_level(gpio_hw, ctx->cs_pins[i], 1);
  }

  // Latch data (LDAC pulse)
  if (ctx->ldac_io_num >= 0) {
    gpio_ll_set_level(gpio_hw, ctx->ldac_io_num, 0);
    for (int n = 0; n < 10; n++)
      asm volatile("nop");
    gpio_ll_set_level(gpio_hw, ctx->ldac_io_num, 1);
  }
}
