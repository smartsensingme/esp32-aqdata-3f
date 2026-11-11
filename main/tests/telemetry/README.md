# Teste do handoff de telemetria

Compila o `app_control.c` real e o PR portátil, com stubs mínimos para os headers
ESP-IDF. Verifica captura incompleta, rejeição de pedidos sobrepostos, proteção
do buffer pronto, repetição e 500 capturas com produtor/consumidor concorrentes.
O teste não reproduz o scheduler/ISR do ESP32 e não mede jitter.

```sh
cmake -S main/tests/telemetry -B /tmp/telemetry-tests
cmake --build /tmp/telemetry-tests
ctest --test-dir /tmp/telemetry-tests --output-on-failure
```

Para verificar acessos concorrentes com um compilador que suporte ThreadSanitizer,
use outro diretório de build e acrescente `-DCMAKE_C_FLAGS='-fsanitize=thread -g'`
à configuração. No macOS, selecione `CMAKE_OSX_SYSROOT` explicitamente se o SDK
padrão não for compatível com o linker instalado.
