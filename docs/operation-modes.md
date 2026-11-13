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
pelo componente rt_diagnostics. O suporte a Wi-Fi/MQTT foi removido. O build mínimo inclui apenas as
dependências declaradas pelo app, sem as pilhas de rede. Nenhum ganho foi alterado nesta simplificação.

## Relatórios

Com `CONFIG_APP_RMS_LOG_ENABLE=y`, um worker Core 0 solicita uma captura a cada
10 segundos usando agendamento absoluto. Captura 1.440 frames (100 ms a 14,4 kHz),
não os dez segundos inteiros. O controle só copia dados durante a captura;
formatação/cálculos ficam no worker, com um único buffer pendente.

A cada captura: `Icomp_mean` (DC/média com sinal), `Icomp_rms` (RMS total) e
`Icomp_ac_rms` (RMS após retirar a média) das três correntes de compensação estimadas a partir dos DACs. Não são leituras de
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

## ADC2 no ESP32-S3

A aquisição atual continua no ADC1 via DMA. A ESP-IDF v6.0 documenta que ADC2
em DMA não é suportado normalmente por resultados instáveis (limitação de
hardware). A opção de forçar ADC2 não remove a errata. Retirar Wi-Fi não torna
essa aquisição confiável. ADC2 oneshot é um caso separado; compartilha recursos
com Wi-Fi, e o driver oferece arbitragem. Não foi feita migração para ADC2.

Referências: [ADC contínuo](https://docs.espressif.com/projects/esp-idf/en/v6.0/esp32s3/api-reference/peripherals/adc/adc_continuous.html#hardware-limitations),
[ADC oneshot](https://docs.espressif.com/projects/esp-idf/en/v6.0/esp32s3/api-reference/peripherals/adc/adc_oneshot.html#hardware-limitations).

Correção de calibração após watchdog IDLE1: PR/PID não são calculados durante
medição de zero; a acomodação usa blocos limitados com ADC parado antes de cada
pausa. A janela final é contínua e usa a mesma LUT, alinhamento, FIR e atividade
SPI no neutro. Não é reprodução da carga computacional dos controladores.
Watchdogs permanecem ativos. Backlog, overflow e dados inválidos rejeitam a
calibração sem gravar NVS, com motivo e máximo de notificações no log.
A execução na placa dessa correção ainda precisa ser verificada.

## Folga de CPU na operação trifásica

Após a calibração corrigida, um ensaio a 160 MHz gravou os três offsets e depois
acionou o watchdog de IDLE1 em malha fechada. A primeira janela indicou 29.198
atrasos em 71.995 ciclos (40,5556%), embora só duas execuções isoladas tivessem
excedido 11.111 ciclos. A espera/despertar também consome o prazo de 70 us. O
backtrace no SPI não demonstra transação travada, e a janela afetada pelo dump
do watchdog não serve para estimar execução normal.

A configuração passou a 240 MHz para aumentar folga. O orçamento nominal de
CPU por frame passa a 16.666 ciclos; o prazo/amostragem continuam 70 us/14,4 kHz
e o SPI permanece configurado para 20 MHz. Ganhos e watchdogs não foram alterados.
O código calcula o orçamento de diagnóstico e timeout SPI pela frequência de
CPU configurada. A validação da folga efetiva exige novo ensaio no hardware.
