# Comparação ADC × multímetro com corrente aplicada — 2026-11-11

O usuário reaplicou ~0,88 A, informando que a corrente sobe perto de 0,90 A com
aquecimento, e mediu **1,76 V no GPIO4**. Foi repetida a aquisição com todos os
DACs fixos em 2048 e sem aplicar a saída PR/PID. Nenhum offset foi gravado.
Rs=1985 Ω, N nominal=2000 e Vref DAC/condicionador=3,36 V mantidos.

## Leituras

| Configuração | Média das 20 janelas ADC impressas | Faixa das médias | Frequência observada por fase |
| --- | --- | --- | --- |
| Temporização exata, SPI neutro repetido | 1,689082 V | 1,687092–1,690771 V | 14.400 Hz |
| Temporização exata, sem SPI durante aquisição | 1,696481 V | 1,693228–1,700403 V | 14.400 Hz |
| Temporização do driver, sem SPI durante aquisição | 1,700733 V | 1,697817–1,704202 V | 14.619,883 Hz |

Durante o último ensaio o usuário confirmou novamente **1,76 V no multímetro**,
para comparação simultânea. Diferença aproximada de 59 mV (~3,4%) em relação à
média das janelas do último ensaio. Média e RMS total ficam muito próximos porque
há um nível DC grande em relação ao RMS AC (~13–14 mV); isso não explica a
diferença de leitura absoluta entre instrumentos.

A corrente primária aquece e varia. Portanto diferenças de poucos mV entre rodadas
não foram atribuídas exclusivamente ao SPI ou ao clock. O que se observou é que
nenhuma dessas duas alterações eliminou a diferença de ~60–70 mV para o multímetro.
Não foi demonstrada uma causa única da discrepância. DMA transporta dados; não se
atribuiu a ele, por si só, mudança de escala.

## Alterações estritamente diagnósticas

`ADC_QUIET_SPI=1` envia e retém os códigos neutros antes de iniciar o ADC, sem
transações DAC durante a aquisição. Uma transação neutra também ocorre ao parar.
`ADC_USE_IDF_TIMING=1` deixa o clock e o trigger escolhidos por
`adc_continuous_config()`/`adc_continuous_start()`, sem os registros manuais para
43,2 kconversões/s. O driver IDF 6 usa divisor para clock digital de 5 MHz e
intervalo inteiro 57, resultando em 43.859,648 conversões/s. O relógio exato usado
normalmente é 3,456 MHz com trigger 40, resultando em 43.200 conversões/s.

As flags só podem ser compiladas com `TUNING_IS_ON=1`,
`TUNING_HOLD_NEUTRAL=1` e demo desligada. Não foram adotadas como configuração de
produção ou como correção do ADC. O teste usa contagem nominal de frames para
as janelas; com clock do driver, o ensaio nominal de 84 s dura cerca de 82,74 s e
a captura nominal de 100 ms dura cerca de 98,50 ms. Como nenhum controle é
aplicado, isso não altera a atuação; não usar o modelo temporal nominal para
controle enquanto esse diagnóstico estiver selecionado.

A rotina neutra passou a receber a corrente externa de referência também para
os relatórios. Ela não informa porcentagem de corrente medida nem escreve zero
quando há primário declarado não nulo: Icomp=0 nesse modo é a ação comandada,
não estimativa da corrente primária. A primeira rodada ainda imprimiu referência
0 A no cabeçalho por uma limitação de metadados já corrigida; os dados ADC dessa
rodada são válidos, mas a condição física era ~0,88 A, não zero.

Estado final: flags diagnósticas ativas, aquisição concluída, quatro DACs em
2048, NVS preservada. O próximo firmware de controle deve voltar
`ADC_QUIET_SPI=0` e `ADC_USE_IDF_TIMING=0`, com validação apropriada antes de
habilitar o atuador. Não se alteraram ganhos nem número de espiras.

## Interpretação e próximo ensaio

Antes de ajustar espiras ou ganho de corrente, validar a conversão ADC com fontes
DC conhecidas e estáveis no próprio GPIO4, no mesmo GND, faixa e configuração.
Separar essa validação da deriva do sensor/condicionador e não ajustar uma reta
com apenas leituras arredondadas, tomadas em condições diferentes.

A [Tabela 5-6 do datasheet ESP32-S3 v2.2](https://documentation.espressif.com/esp32-s3_datasheet_en.pdf#page=66)
indica erro total calibrado de até ±50 mV em ATTEN3, nas condições indicadas pelo
fabricante (25 °C, DC, capacitor externo de 100 nF e Wi-Fi desabilitado). Isso
mostra que a calibração oficial não é garantia de coincidência perfeita com o
multímetro; não torna automaticamente aceitável a discrepância de ~60 mV deste
ensaio nem identifica sua causa. Calibração externa pode ser necessária para a
exatidão desejada, depois de verificar referência, fonte e conexão.

## Registros

- [Original: gravação](adc176-flash.log), [monitor](adc176-monitor.log)
- [SPI suspenso: gravação](quiet-spi-flash.log), [monitor](quiet-spi-monitor.log)
- [Clock IDF: gravação](driver-adc-flash.log), [monitor](driver-adc-monitor.log)

Builds e `git diff --check` passaram. O teste host do ensaio finito foi atualizado
para aceitar a referência externa no modo neutro; controles e estimativas de
corrente não foram usados para corrigir a leitura ADC.
