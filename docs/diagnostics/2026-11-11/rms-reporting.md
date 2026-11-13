# Relatório de RMS em DC — 2026-11-11

Após observar 80–120 mV pico a pico no osciloscópio, o usuário pediu RMS também
para DC e sugeriu usá-lo na calibração. A aplicação passou a separar média, RMS
total e RMS AC. O offset continua sendo média aparada de tensão filtrada com ganho
DC normalizado. A igualdade visual RMS/DC não prova ruído nulo nem justifica usar
RMS como offset.

O firmware de relatório finito foi gravado e executado usando a última condição
confirmada: corrente primária retirada, Rs recolocado. A NVS foi somente lida.
Ganhos e referências de tensão não foram ajustados. Offset carregado R=1,584847 V.

| Métrica | Patamar neutro | Controle ativo |
| --- | --- | --- |
| Média ADC | 1,587245 V | 1,579163 V |
| RMS total ADC | 1,587306 V | 1,579223 V |
| RMS AC ADC | 13,952 mV | 13,784 mV |
| RMS erro filtrado | 9,125 mV | 10,457 mV |
| Média erro filtrado | +2,419 mV | −5,738 mV |
| RMS AC erro filtrado | 8,798 mV | 8,742 mV |

A diferença RMS−média foi só ~61 µV no neutro, apesar do RMS AC de ~14 mV. Isso
explica por que um instrumento pode mostrar o mesmo número DC/RMS arredondado.
Não se deduz RMS a partir de Vpp sem conhecer forma, banda e janela do ruído.

Em controle, houve compensação aparente média de −0,128651 A e RMS de 0,130672 A
com corrente primária informada nula. Sem saturação, mas a leitura de zero ainda
não ficou correta. As médias variaram entre as janelas; a deriva/falta de zero
não está resolvida. Não se atribuiu a causa exclusivamente a DMA nem a ruído e
não se alteraram ganhos ou escala para esconder esse resultado.

A leitura ADC do firmware também não coincide com os ~1,68 V citados no
osciloscópio. Comparar novamente no mesmo ponto/GND, banda e estado de atuação;
a escolha entre média e RMS explica apenas dezenas de µV neste ensaio, não uma
diferença próxima de 0,1 V.

Estado final: `TUNING_IS_ON=1`, `TUNING_DC_TEST=1`,
`TUNING_DC_REFERENCE_A=0.0f`, teste concluído, ADC parado e quatro DACs neutros.
GPIO7 é ignorada nesse modo. Um reset repete o ensaio de zero. As mudanças na
calibração filtrada ainda não foram usadas para regravar a NVS neste ensaio.

[Gravação](rms-zero-flash.log), [monitor](rms-zero-monitor.log).

Build e `git diff --check` passaram. Testes de estatística DC+AC e normalização
FIR real passaram com sanitizadores; captura/impressão ADC/FIR, handoff concorrente,
ensaio finito, persistência e polaridade também passaram. O custo adicional da
captura normal de sinais ainda precisa ser medido especificamente em hardware.
