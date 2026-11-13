idf_component_register(SRCS "main.c" "app_control.c" "app_parallel.c" "app_diagnostics.c" "app_fir.c" "app_offsets.c" "app_polarity.c" "app_mqtt.c"
                       INCLUDE_DIRS ""
                       REQUIRES esp_anti_windup esp_pid esp_driver_gpio esp_rt_diagnostics esp_rt_diagnostics_reporter esp_pr esp_fir esp_mcp4922 esp_timer esp_driver_gptimer esp_adc esp_hw_support nvs_flash esp_wifi esp_netif esp_event mqtt)
