idf_component_register(SRCS "main.c" "app_control.c"
                       INCLUDE_DIRS ""
                       REQUIRES esp_mcp4922 esp_timer esp_driver_gptimer esp_adc nvs_flash esp_wifi esp_netif esp_event)
