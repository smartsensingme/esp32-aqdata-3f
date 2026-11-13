# Testes de calibração

Veja [o procedimento e os comandos](../../../docs/offset-calibration.md).
Os testes compilam `app_offsets.c` real com stubs de GPIO/ADC/NVS. Não alteram a
flash da placa. As falhas e os dados são injetados pelo teste, sem emular o NVS
real nem o scheduler do ESP32.
