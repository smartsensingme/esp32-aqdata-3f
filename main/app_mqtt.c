#include "app_mqtt.h"

#if RUN_WITH_WIFI

#include "esp_crt_bundle.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "mqtt_client.h"
#include "nvs_flash.h"
#include <stdio.h>

#define WIFI_SSID "defranca.net-6G"
#define MQTT_BROKER "mqtts://mqtt.smartsensing.me:8883"
#define MQTT_USER "esp32s3"
#define MQTT_TOPIC "/ssme/measurement/current"

// Inclui as senhas a partir de um arquivo ignorado pelo git
#include "secrets.txt"

// Potência do WI-FI em dBm. Valores menores = menos ruído.
#define WIFI_DBPOWER 8

static const char *TAG = "app_mqtt";

static esp_mqtt_client_handle_t mqtt_client = NULL;

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    esp_wifi_connect();
  } else if (event_base == WIFI_EVENT &&
             event_id == WIFI_EVENT_STA_DISCONNECTED) {
    esp_wifi_connect();
    ESP_LOGI(TAG, "Tentando reconectar ao Wi-Fi...");
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    ESP_LOGI(TAG, "Wi-Fi Conectado! IP: " IPSTR, IP2STR(&event->ip_info.ip));
  }
}

static void wifi_init_sta(void) {
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  esp_netif_create_default_wifi_sta();

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

  wifi_config_t wifi_config = {
      .sta =
          {
              .ssid = WIFI_SSID,
              .password = WIFI_PASS,
              .threshold.authmode = WIFI_AUTH_WPA2_PSK,
          },
  };
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
  ESP_ERROR_CHECK(esp_wifi_start());

  // CRÍTICO PARA O ADC: Desliga o Modem Sleep para estabilizar o VDD33
  ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

  // Limita a potência do rádio para reduzir o ruído sobre o ADC e SPI (unidade é 0.25 dBm)
  ESP_ERROR_CHECK(esp_wifi_set_max_tx_power(4 * WIFI_DBPOWER));
}

static void mqtt_app_start(void) {
  esp_mqtt_client_config_t mqtt_cfg = {
      .broker.address.uri = MQTT_BROKER,
      .broker.verification.crt_bundle_attach = esp_crt_bundle_attach,
      .credentials.username = MQTT_USER,
      .credentials.authentication.password = MQTT_PASS,
  };
  mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
  esp_mqtt_client_start(mqtt_client);
}

void app_mqtt_init(void) {
  esp_err_t ret_nvs = nvs_flash_init();
  if (ret_nvs == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret_nvs == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret_nvs = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret_nvs);

  wifi_init_sta();
  mqtt_app_start();
}

void app_mqtt_publish_rms(float rms_r, float rms_s, float rms_t) {
  if (mqtt_client != NULL) {
    char json_payload[100];
    snprintf(json_payload, sizeof(json_payload),
             "{\"r\":%.3f,\"s\":%.3f,\"t\":%.3f}", rms_r, rms_s, rms_t);
    esp_mqtt_client_publish(mqtt_client, MQTT_TOPIC, json_payload, 0, 0, 0);
    ESP_LOGI(TAG, "MQTT Publicado: %s", json_payload);
  }
}

#endif // RUN_WITH_WIFI
