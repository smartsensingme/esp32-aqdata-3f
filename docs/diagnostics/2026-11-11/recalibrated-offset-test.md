# Reteste após nova calibração de offset — 2026-11-11

O usuário confirmou corrente primária de aproximadamente 0,9 A reaplicada após
calibrar o zero. O firmware ainda continha `R_LOAD=2000.0f`; foi corrigido para
**1985 Ω**, conforme a medição informada. A relação de espiras permaneceu no
valor nominal 2000, sem ajustar fatores para forçar a leitura a 0,9 A.

Novo registro carregado da NVS:

```
R=1.578955 V, S=0.357334 V, T=0.036654 V
```

## Ensaios com resistor ausente

O primeiro ensaio DC terminou em 8,011 s de uptime por limite atingido por 100 ms.
Erro médio no neutro: +257,942 mV; última amostra filtrada: +252,870 mV. O erro
quase não diminuiu com a atuação. Não houve janela final válida para reportar
corrente em regime; não interpretar a corrente do patamar neutro como medição
válida da corrente primária.

Um ensaio independente em malha aberta usou a API SPI de alto nível, DAC R
2048→2176 (128 códigos), com corrente primária DC mantida. O objetivo era verificar
a resposta local do atuador, não recalibrar nem validar polaridade em corrente
zero. Resultado: −2,27 mV de resposta, limiar de ruído 29,70 mV, deriva 1,08 mV.
Portanto inconclusivo, diferente dos ~−63 mV observados anteriormente.

O usuário então identificou que havia retirado o resistor de **1,985 kΩ** para
medi-lo e não o havia recolocado. Isso explica a perda de atuação no caminho de
compensação. Não foi necessário aumentar ganhos nem limites para compensar a
montagem incompleta. A NVS permaneceu inalterada nos ensaios.

- [DC com resistor ausente: gravação](dc-recal-flash.log)
- [DC com resistor ausente: monitor](dc-recal-monitor.log)
- [Degrau com resistor ausente: gravação](recal-response-flash.log)
- [Degrau com resistor ausente: monitor](recal-response-monitor.log)

## Repetição com resistor recolocado

O usuário confirmou que recolocou o resistor e reaplicou os 0,9 A. O ensaio DC
finito foi preparado e gravado novamente com os mesmos ganhos e limites.

Com o resistor recolocado, o ensaio concluiu normalmente: compensação estimada
0,777339 A (−13,63% relativo a 0,9 A), erro médio +0,175 mV e nenhuma limitação.
Ao retirar a corrente primária, mantendo a montagem completa, o teste de zero
mostrou erro no neutro de −31,707 mV e compensação aparente de −0,109498 A.
A subtração dos zeros entre ensaios dá ~0,886837 A, mas não é calibração definitiva
nem substitui uma medição independente da corrente e da estabilidade do zero.

Foi tentada nova calibração oneshot completa com GPIO7 baixo, que gravou
R=1,591725 V. O zero continuou incorreto em operação. Para consistência, a captura
de offset foi movida para a aquisição contínua, sem concluir que DMA muda a escala.
A primeira captura DMA bruta gravou R=1,584352 V; com acomodação de 10 s, o registro
usado no ensaio seguinte foi R=1,584847 V. A indicação de zero ainda mostrou deriva.
O novo relatório separa média, RMS total e RMS AC para não confundir ruído com zero.
Veja [o ensaio mais recente](rms-reporting.md).

Não se ajustaram ganhos nem fator de espiras para forçar 0,9 A. A causa da diferença
de zero/escala ainda não está isolada; a nova calibração com FIR normalizado precisa
de validação física. Registros adicionais:

- [Resistor recolocado: monitor](dc-rs-restored-monitor.log)
- [Teste de zero com montagem completa](zero-complete-monitor.log)
- [Recalibração oneshot](zero-recal-monitor.log)
- [Captura DMA bruta](dma-cal-monitor.log)
- [Captura DMA bruta com acomodação maior](dma-cal-settle-monitor.log)
