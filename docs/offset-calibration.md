# Calibração de offsets das três fases

Com montagem completa, fontes ligadas e corrente primária nula, conecte GPIO7
ao GND e resete com `CALIB_IS_ON=0`. Aguarde `Calibration saved R/S/T` e solte
a GPIO antes do próximo reset com corrente. `TEST_IS_ON` não impede calibração.
Os DACs permanecem no neutro nominal 2048. Sem registro válido, o padrão é
1,65 V para cada fase. A calibração não determina polaridade.

O proprietário do ADC no Core 1 usa a mesma LUT calibrada, DMA, alinhamento FOH,
FIR e escritas SPI no neutro da operação. Os controladores são ignorados,
pois suas ações não seriam aplicadas. A acomodação tem dez blocos de um segundo
de amostragem, com ADC parado/esvaziado e pausa de um tick entre blocos para
permitir executar IDLE1. O último bloco emenda na captura de 1.440 frames/100 ms,
sem pausa ou descarte dentro da janela medida. A duração total inclui as pausas. Guarda ADC bruto e FIR normalizado em DC para R/S/T. Só neutro é enviado aos DACs.

Depois de parar ADC e esvaziar o pool, calcula e mostra para cada fase antes e
depois do FIR: N, média/DC, RMS total, desvio padrão populacional, mínimo e
máximo. O offset usado continua sendo a média aparada do FIR: retira 144 valores
de cada extremo e calcula a média dos 1.152 restantes. A média integral
reportada e o offset aparado podem diferir em presença de outliers. Nunca usa
RMS como offset. Os buffers temporários consomem 34.560 bytes, liberados antes
do controle normal. Ordenação, estatística e NVS não ocorrem durante aquisição.

Persiste UM blob na namespace `adc_offsets`, chave `calibration_v2`:
`uint32 version=2`, `uint32 sample_count=1440`, três floats de offset R/S/T,
três relatórios ADC e três FIR. Cada relatório tem cinco floats nesta ordem:
média, RMS total, desvio padrão, mínimo e máximo. Layout nativo ESP32-S3,
little-endian, IEEE float de 32 bits (140 bytes). Não é protocolo portátil.
Apenas `calibration_v2` é lido. Registros antigos são ignorados; na ausência de
v2 válido, usa 1,65 V em cada fase até nova calibração. Não é necessário apagar
a partição NVS para recalibrar.
Não há erase/migração automática na inicialização. A RAM só recebe novos offsets
após sucesso do commit, e FIR/controladores são reinicializados antes do loop.

Amostras inválidas em QUALQUER fase, erro ADC/SPI, backlog maior que quatro,
overflow DMA ou falha NVS impedem iniciar a operação. Não se grava um subconjunto
de fases. Falha DAC retém contexto e requer reset. Gravação sob queda de energia
usa garantias NVS e ainda não foi testada fisicamente por esta mudança.

Não remova o sensor para calibrar: campo residual, deriva e montagem incompleta
podem aparecer como offset. O firmware não distingue esses efeitos. Os testes
host cobrem estatísticas, média aparada, dados inválidos, falha de commit,
recarregamento v2 e rejeição de versões antigas; não simulam ADC/DMA.
