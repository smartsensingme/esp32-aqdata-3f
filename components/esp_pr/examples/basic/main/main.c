#include "esp_check.h"
#include "esp_log.h"
#include "esp_pr.h"

static const char *TAG = "esp_pr_example";
static esp_pr_model_t model;
static esp_pr_t controller;

void app_main(void) {
  const unsigned orders[] = {1U, 3U, 5U, 7U};
  const float gains[] = {100.0f, 20.0f, 10.0f, 5.0f};
  const float damping_hz[] = {5.0f, 5.0f, 5.0f, 5.0f};
  esp_pr_resonator_config_t branches[4];

  ESP_ERROR_CHECK(
      esp_pr_make_harmonics(branches, 4U, 60.0f, orders, gains, damping_hz));

  const esp_pr_config_t config = {
      .sample_rate_hz = 14400.0f,
      .kp = 1.0f,
      .output_min = -6.0f,
      .output_max = 6.0f,
      .anti_windup_tracking_time_s = 0.010f,
      .resonators = branches,
      .resonator_count = 4U,
  };
  ESP_ERROR_CHECK(esp_pr_prepare(&model, &config));
  ESP_ERROR_CHECK(esp_pr_init(&controller, &model));

  float output = 0.0f;
  ESP_ERROR_CHECK(esp_pr_update(&controller, 0.1f, 0.0f, &output));
  ESP_LOGI(TAG, "first controller output: %.6f", (double)output);
}
