# Média, RMS e offset

Para uma janela de amostras:

```
media = mean(x)
RMS_total² = media² + RMS_AC²
RMS_AC = RMS(x - media)
```

Em sinal DC, RMS_AC inclui ruído/ripple e deriva dentro da janela. Em sinal AC,
também inclui a corrente/sinal desejado; não deve ser chamado automaticamente de
ruído. Pico a pico de 80–120 mV não determina RMS sem conhecer a forma, banda e
janela de medição. O RMS total é sempre não negativo e não substitui a média na
calibração de um erro com sinal.

Exemplo: 1,68 V DC com 20 mV RMS AC produz cerca de 1,68012 V RMS total. Média e
RMS podem aparecer iguais no osciloscópio por arredondamento; isso não comprova
que não há ruído nem que seu valor médio físico seja nulo.

## Relatórios

`CONFIG_APP_RMS_LOG_ENABLE=y` mantém a compensação estimada RMS mesmo em DC e,
na operação normal, acrescenta média, RMS total e RMS AC do ADC bruto e do sinal
FIR normalizado, somente nas fases habilitadas. A captura tem 1.440 frames,
100 ms a 14,4 kHz (5 ciclos de 50 Hz e 6 de 60 Hz). É solicitada pelo worker Core 0,
com política existente de um buffer pendente, sem bloqueio do controle. Há 34.560
bytes de novos buffers de sinais; cálculos de estatística e logs ficam no worker.
O custo de cópia durante a captura ainda precisa de validação específica na placa.

O ensaio finito imprime RMS de ADC, erro filtrado e corrente de compensação ao
parar. O teste de polaridade imprime RMS de cada patamar. A calibração imprime
média e RMS da tensão filtrada capturada. As janelas têm durações e bandas
diferentes; não comparar diretamente Vpp de osciloscópio em banda larga com RMS
ADC/FIR como se fossem a mesma medição.

## Calibração

O offset continua sendo a **média aparada**, não RMS. As amostras de calibração
agora usam o mesmo caminho DMA/FOH/FIR e taxa do controle. O ganho DC do FIR atual,
obtido dos coeficientes reais, é 1,00951398. Reconstrução usada para a tensão:

```
V_filtrada_normalizada = offset_anterior + erro_filtrado / ganho_DC_FIR
```

Isso permite usar o filtro sem embutir seu ganho no offset. Depois de 10 s de
acomodação sob carga de processamento e SPI neutro, são capturadas 1.440 amostras
por fase. Descartam-se 10% de cada extremidade, preservando o sinal da média. Só
as fases habilitadas atualizam o registro; as demais preservam os offsets.
ADC parado antes de ordenar, calcular e gravar NVS. Não há gravação no laço ativo.

A mudança de aquisição é uma medida de consistência, **não uma demonstração de
que DMA altera a escala ADC**. As causas da deriva e da diferença para a tensão
medida externamente ainda não foram isoladas. Depois de remover o offset, o RMS
do erro pode permanecer positivo por causa da componente AC.

## Verificação

Build ESP32-S3 e testes host passaram: média/RMS com DC+AC conhecido e com média
negativa, reconstrução DC usando o FIR real, captura e impressão ADC/FIR, handoff
concorrente, persistência dos offsets e ensaio finito. Estatística e FIR foram
verificados com sanitizadores. Não foram alterados ganhos nem referências para
forçar concordância com o osciloscópio.

O novo relatório finito foi gravado/executado em corrente primária zero, sem
regravar NVS. Os logs normais ADC/FIR passaram em teste host; a calibração com a
nova normalização FIR ainda precisa de validação física, não foi acionada nesse
ensaio finito. A placa terminou no neutro.

[Registro do ensaio de RMS](diagnostics/2026-11-11/rms-reporting.md).
