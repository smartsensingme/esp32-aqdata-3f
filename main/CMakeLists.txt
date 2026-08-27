idf_component_register(SRCS "main.c" "app_control.c" "app_mqtt.c" "fir_pmr.c" "pr_controller.c"
                       INCLUDE_DIRS ""
                       REQUIRES esp_mcp4922 esp_timer esp_driver_gptimer esp_adc esp_hw_support nvs_flash esp_wifi esp_netif esp_event mqtt)
