# Ensaios Kp em hardware — 13/11/2026

Pedido: comparar a leitura com multímetro indicando inicialmente 0,547 A e
avaliar aumento de Kp. Não houve recalibração nem alteração de NVS: todas as
partidas carregaram offsets R/S/T = 1,545657 / 0,513952 / 0,097400 V.
Somente R possui sensor; S/T não são resultados de medição válidos.

## Grandezas comparadas

O log original com Kp=0,5 indicava, aos 60 s: média/DC de +0,529064 A e RMS
total de 0,762462 A. Retirando DC, o RMS AC é 0,549035 A. Comparar 0,762462 A
diretamente com um instrumento em AC que rejeita DC mistura grandezas.
A inclusão de DC/banda do instrumento não foi comprovada neste ensaio: a tabela
abaixo usa RMS AC para a comparação e não certifica a exatidão metrológica.
O firmware agora imprime explicitamente `Icomp_ac_rms` para R/S/T, além do total
e da média. Ambos vêm do comando DAC e da escala elétrica configurada, não de
medição direta da corrente na bobina. A componente DC significativa requer
investigação própria; não se alterou offset/Ki para ocultá-la.

## Sequência real de testes

| Kp | RMS AC estimado aos 60 s | Multímetro informado ao fim | Diferença | Residual FIR R RMS AC |
|---|---:|---:|---:|---:|
| 1,0 | 0,553325 A | 0,546 A | +1,342% | 127,720 mV |
| 2,0 | 0,573779 A | 0,543 A | +5,668% | 117,809 mV |
| 0,5 (confirmação) | 0,538001 A | 0,543 A | −0,921% | 129,521 mV |

Referências solicitadas ao usuário logo após cada coleta; janela do firmware
em aproximadamente 60 s, captura serial de cerca de 70 s nos dois primeiros
ensaios e 63 s na confirmação. Não são medições simultâneas rastreáveis. A carga
variou entre ensaios. Não se identifica planta ou ganho ótimo com esta tabela.
Os valores acima são snapshots de 100 ms, não médias dos 60 segundos.

Ganhos mantidos em TODOS os ensaios: Ki=10, Kr60=14, Kr120=1, Kr180=3,
Kr300=2, Kr420=1, damping 5 Hz e tracking comum de 10 ms. Mesmos ganhos
nas três fases, CPU 240 MHz e SPI nos pinos novos. No estágio PID, Kp=Kd=0.
Apenas o proporcional compartilhado no PR foi alterado e depois restaurado.

Cada coleta registrou 863.996 ciclos em 12 janelas. Houve um atraso de prazo
em cada ensaio, no transiente inicial de publicação; nenhuma perda de pool,
erro de leitura, backlog ou watchdog nos logs. Não houve clipping de DAC R
nos relatórios detalhados: códigos 1923..2934 (Kp1), 1917..2991 (Kp2) e
1923..2892 (Kp0,5). Isto não prova ausência de clipping fora das capturas.

## Resultado deixado

Kp foi restaurado a **0,5 no código e na placa**. Ki e PR não mudaram.
Kp maior reduziu o residual, mas piorou a concordância AC observada; não há
base para mantê-lo apenas para aproximar o RMS total do multímetro. O erro
aparente de cerca de 39% incluía uma componente DC de aproximadamente 0,52 A.
Na confirmação: DC=+0,519083 A, total=0,747591 A, AC=0,538001 A.
A malha permanece contínua; não há critério de parada novo ou ajuste de escala.

Build/flash passaram nos três ensaios. Testes host de controle paralelo e
telemetria passaram; teste de relatório inclui sinal com DC+AC conhecido para
validar o novo campo. A captura física não substitui análise de estabilidade,
resposta a degrau, banda do instrumento ou repetibilidade em outras correntes.

Logs: [baseline do usuário](kp05-user-baseline.log), [Kp1](kp1-monitor.log),
[Kp2](kp2-monitor.log), [confirmação Kp0,5](kp05-confirm-monitor.log).
Valores estruturados: [kp-comparison.json](kp-comparison.json).
Logs preservados com normalização apenas de espaços finais e quebras de linha.
