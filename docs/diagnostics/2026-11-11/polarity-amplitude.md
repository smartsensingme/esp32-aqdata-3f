# Amplitude para identificação da polaridade — 2026-11-11

Solicitação: aumentar gradualmente o degrau DAC, gravar e executar a identificação
na placa até obter resposta conclusiva. Malha aberta, `CALIB_IS_ON=1`,
`TEST_IS_ON=0` preservado. Somente R foi excitado; outros DACs no neutro.
Os limiares e o algoritmo não foram alterados para obter aprovação.

Porta: `/dev/cu.usbmodem831101`. Um monitor ESP-IDF existente deste projeto foi
encerrado para liberar a porta. Builds e gravações com verificação de hash
passaram. Cada monitor iniciou uma nova partida do teste.

| Degrau | Códigos DAC | Resposta GPIO4 | Limiar | Deriva | Resultado |
| --- | --- | --- | --- | --- | --- |
| 64 | 2048 → 2112 | −32,10 mV | 36,59 mV | 0,44 mV | Inconclusivo, encerrou no primeiro ciclo |
| 128 | 2048 → 2176 | −62,44 a −63,66 mV | 36,82 a 39,35 mV | 0,33 a 2,18 mV | Compatível, cinco ciclos |

No teste de 128, GPIO4 neutro ficou aproximadamente em 1,559–1,562 V, e excitado
em 1,497–1,499 V. DAC crescente reduziu o erro medido: sinal local compatível
com realimentação negativa no controlador atual. Não foi necessário aumentar
até 256 nem até o máximo do DAC.

Estado final: fonte e firmware gravado usam `CALIB_DAC_STEP_CODES=128`.
O teste terminou, todos os DACs ficaram no neutro e a malha continua parada.
Não foi gravada polaridade nem offset em NVS, e não houve inversão automática
do controlador. O monitor usado para captura foi encerrado após a aquisição.

A tensão física no DAC e a corrente da bobina não foram medidas externamente
pelo agente. A condição de corrente primária nula deve ser garantida pela
montagem/operador; não é verificada por este teste. A resposta local não prova
estabilidade em malha fechada. O neutro ADC observado difere do default 1,65 V:
calibrar o offset na condição física correta antes de testar controle fechado.

Registros completos:

- [Gravação, 64 códigos](polarity-64-flash.log)
- [Monitor, 64 códigos](polarity-64-monitor.log)
- [Gravação, 128 códigos](polarity-128-flash.log)
- [Monitor, 128 códigos](polarity-128-monitor.log)
