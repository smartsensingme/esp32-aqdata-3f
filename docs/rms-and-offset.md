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

`CONFIG_APP_RMS_LOG_ENABLE=y`: resumo DC/RMS das três correntes estimadas a
cada 10 s; detalhes da fase R antes/depois do FIR a cada 60 s. Cada captura tem
1.440 frames/100 ms. Estatísticas e logs ficam no Core 0; o controle faz cópia
limitada durante a captura. Desvio padrão é populacional e corresponde ao RMS
sem DC. Calibração mostra/persiste ADC/FIR das três fases. Veja
[operação atual](operation-modes.md) e [calibração](offset-calibration.md).

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

O relatório de corrente agora expõe `Icomp_ac_rms` além de `Icomp_mean` e
`Icomp_rms`. Para comparar com um multímetro em corrente AC que rejeita DC, use
a componente AC; para AC+DC, use RMS total. A indicação True RMS sozinha não
define a inclusão de DC, a banda ou a precisão do instrumento. Os valores são
estimados a partir do comando DAC, não medidos por um ADC no resistor secundário.

## Residuais por frequência na fase R

O relatório detalhado também calcula bins DFT nominais em 60, 120, 180, 300 e
420 Hz para ADC e FIR. A média é removida antes da projeção, e o módulo é
convertido para RMS da senoide correspondente. A janela de 100 ms contém um
número inteiro de ciclos dessas frequências a Fs=14,4 kHz. É uma estimativa por
bin, não análise de banda completa nem medição da frequência real da rede;
desvios de frequência e janelamento podem causar leakage. Não representa todo
o ruído ou harmônicos fora dos cinco bins.

O cálculo usa os buffers já existentes e roda somente no worker Core 0 após a
captura, sem alocação ou novas operações por frame no laço de controle. Foram
testados DC mais senoides conhecidas em 60/180 Hz, rejeição do bin 120 Hz sem
sinal e argumentos inválidos. Compare o ramo PR pelo seu bin e pelo residual
global, acompanhando também esforço/limites do atuador e a corrente estimada.
