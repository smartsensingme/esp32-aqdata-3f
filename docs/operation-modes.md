# Operação atual e relatórios

Estado anterior de comissionamento preservado no commit `86a1fe0`, incluindo
fontes, modos temporários e registros dos ensaios. Os arquivos em
`docs/diagnostics/` são históricos, não instruções para as flags atuais.

## Modos em main/main.c

- `CALIB_IS_ON=1`: prioridade máxima, preserva a verificação automática de
  polaridade da fase R/GPIO4 existente. Informa resposta positiva/negativa ou
  inconclusiva, deixa os DACs neutros e não inicia controle nem grava polaridade.
- `CALIB_IS_ON=0`, `TEST_IS_ON=1`: três senos de 60 Hz nos DACs, defasados 120°.
  ADC, alinhamento, FIR e controladores continuam calculando; as ações dos
  controladores são descartadas. A telemetria usa os códigos de seno realmente
  enviados, não as ações descartadas; corrente estimada no demo não é validação
  de medição de corrente primária.
- Ambos zero: malha fechada contínua R/S/T, mesmos ganhos em `app_control.h` e
  estados independentes. O `esp_anti_windup` coordena PR+I por fase. Não há mais
  máscara R-only ou encerramento programado de sintonia.

GPIO7 baixo no reset, fora do modo de polaridade, pede calibração dos três
offsets. Não é outro modo de compilação. Montagem completa, fontes ligadas e
corrente primária nula nas três fases são pré-condições. DACs ficam em 2048;
calibração termina antes de iniciar o modo escolhido. Solte GPIO7 antes de
reiniciar com corrente. Padrão sem registro válido: 1,65 V em cada fase.

Foram removidos `TUNING_IS_ON`, `TUNING_DC_TEST`, `TUNING_AC_TEST`,
`TUNING_HOLD_NEUTRAL`, `TUNING_DC_REFERENCE_A`, `ADC_QUIET_SPI`,
`ADC_USE_IDF_TIMING`, captura finita de diagnóstico e desativação de RMS para
comparação de jitter. Os diagnósticos contínuos de timing permanecem disponíveis
pelo componente rt_diagnostics. Wi-Fi/MQTT continuam desativados na configuração
atual. Nenhum ganho foi alterado nesta simplificação.

## Relatórios

Com `CONFIG_APP_RMS_LOG_ENABLE=y`, um worker Core 0 solicita uma captura a cada
10 segundos usando agendamento absoluto. Captura 1.440 frames (100 ms a 14,4 kHz),
não os dez segundos inteiros. O controle só copia dados durante a captura;
formatação/cálculos ficam no worker, com um único buffer pendente.

A cada captura: `Icomp_mean` (DC/média com sinal) e `Icomp_rms` (RMS total) das
três correntes de compensação estimadas a partir dos DACs. Não são leituras de
um segundo sensor de corrente. A cada sexta captura, aproximadamente 60 s:
relatório R adicional de DAC, mínimo/máximo dos códigos, ADC calibrado antes do
FIR e tensão reconstruída após FIR normalizado em DC. Para ADC/FIR: média/DC,
RMS total, desvio padrão populacional e mínimo/máximo. DC e média são a mesma
grandeza; desvio padrão populacional equivale ao RMS da componente sem DC.
Para uma entrada AC, isso inclui o sinal desejado, não apenas ruído.

Há latência de captura e escalonamento. O primeiro relatório pode atrasar caso
uma calibração de partida ainda esteja terminando. Os diagnósticos de timing
mantêm sua cadência própria e não foram substituídos pelos relatórios elétricos.

## Validação e limites

Não houve flash nem alteração da NVS física nesta mudança. Três modos e
calibração precisam de validação no hardware com a montagem de três fases.
Conexões antes usadas somente na fase R não validam malha S/T. Erros de
transação DAC continuam parando aquisição; a remoção dos ensaios temporários
não remove tratamento de falhas de periféricos ou watchdogs.

Verificações desta revisão: build ESP-IDF passou em malha fechada, demo e
polaridade; artefato final restaurado para ambos os flags zero. Passaram sete
testes host: offsets, dois de estatística/FIR, controle paralelo, dois de
telemetria e polaridade. Não são provas de deadlines no hardware.
