#pragma once

#define RUN_WITH_WIFI 1 // Altere para 0 para testar sem Wi-Fi e MQTT

#if RUN_WITH_WIFI

void app_mqtt_init(void);
void app_mqtt_publish_rms(float rms_r, float rms_s, float rms_t);

#endif // RUN_WITH_WIFI
