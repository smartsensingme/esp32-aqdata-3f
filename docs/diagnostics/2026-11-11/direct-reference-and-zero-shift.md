# Referência primária direta e mudança de zero — 2026-11-11

O usuário mediu ~0,89 A diretamente com multímetro em série somente no ramo
primário e confirmou que manteve essa ligação durante o ensaio. Depois informou
~0,90 A. Vref do DAC foi medida em 3,36 V e o código passou a usar 3,36 V em vez
de 3,30 V para a conversão de corrente. Isso não altera os códigos enviados ao
DAC nem os ganhos; a mudança isolada de escala é de 1,82%.

| Ensaio | Referência informada | ADC antes de compensar | Icomp média estimada | Erro médio filtrado ativo |
| --- | --- | --- | --- | --- |
| Primeira partida, Vref corrigida | 0,89–0,90 A | 1,756225 V | 0,621271 A | +0,271 mV |
| Repetição, mesmo firmware/montagem | ~0,90 A | 1,758047 V | 0,626503 A | −0,294 mV |

Ambos concluíram sem saturação e voltaram ao neutro. O cabeçalho do firmware
usava referência 0,890 A; a segunda leitura externa era aproximadamente 0,90 A.
A pequena variação da referência não explica a diferença em relação ao ensaio
anterior (~0,843 A reescalados). O estímulo ADC antes da atuação também mudou.
Não se ajustaram ganhos ou N para forçar concordância.

O usuário então retirou apenas a corrente primária, mantendo núcleo, sensor,
condutor e ligação do multímetro. Medição de 84 s com DACs fixos em 2048 encontrou
média ADC perto de **1,470 V**, cerca de 108 mV abaixo do offset salvo de 1,578633 V.
RMS AC era ~12,5 mV. Essa diferença sistemática não é explicada somente pela
comparação entre média e RMS. O usuário informou cerca de **1,6 V no multímetro**
no GPIO4; foi repetida aquisição com relatórios RMS durante o período ativo,
sem regravar a NVS, para evitar comparar apenas com um resultado depois de parar.

Não foi isolada a causa da alteração de zero: entrada/referência, montagem,
conversão ADC ou estado magnético continuam hipóteses. Não se atribuiu a mudança
exclusivamente à histerese, ruído, DMA ou espiras. A calibração de 2119 espiras
não foi aplicada. Com a leitura anterior de 880 mV em Rs e referência primária
0,89 A, a relação seria ~2008 espiras, mas as leituras não foram sincronizadas.

- [Primeira gravação](direct089-flash.log), [monitor](direct089-monitor.log)
- [Repetição sem alterar firmware](direct090-repeat-monitor.log)
- [Zero após ciclo: gravação](postcycle-zero-flash.log), [monitor](postcycle-zero-monitor.log)
- [Zero com relatórios ao vivo: gravação](live-zero-flash.log), [monitor](live-zero-monitor.log)

O próximo pedido do usuário reaplicou ~0,88 A e informou 1,76 V no GPIO4. Veja a
[comparação ADC × multímetro](adc-voltage-comparison.md). Os ensaios não dão base
para adotar um número físico de espiras a partir de um único fator aparente.
