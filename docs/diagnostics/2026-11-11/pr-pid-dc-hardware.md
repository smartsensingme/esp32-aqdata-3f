# PR + esp_pid: ensaio DC em hardware — 2026-11-11

Usuário autorizou gravação e ajuste de ganhos se necessário, e confirmou durante
o ensaio que a corrente de referência de aproximadamente **0,9 A** continuava
aplicada (4,95 V / 5,5 Ω). O agente não mediu independentemente a corrente primária,
Rs, referências DAC/condicionador ou corrente real da bobina.

Foram gravadas e executadas duas partidas do ensaio finito em
`/dev/cu.usbmodem831101`. Os hashes de gravação foram verificados. Configuração:

- R/GPIO4 somente; S/T e quarto DAC neutros.
- PR: Kp=1, Kr60=10, amortecimento 5 Hz; demais harmônicos zero.
- PID: Kp=0, Ki=2, Kd=0; tracking de 10 ms.
- Orçamentos PR/I de 10%/90% do limite total de ±2 V software (~±1,10 A
  primários equivalentes, com integral limitada a ~±0,99 A).
- 4 s neutro + 30 s de controle, estatísticas dos últimos 10 s ativos.
- Offset R carregado: 1,545959 V; offsets preservados na NVS.
- Conversão nominal: Rs=2 kΩ, referências 3,3 V, 2.000 espiras / uma passagem.

## Resultados

| Métrica | Primeiro ensaio | Repetição |
| --- | --- | --- |
| Erro médio neutro | +288,174 mV | +294,340 mV |
| Compensação equivalente média estimada | ~+0,826 A | +0,842942 A |
| Compensação equivalente RMS estimada | ver nota numérica | 0,842959 A |
| Erro médio em controle | +0,119 mV | +0,388 mV |
| Erro AC RMS em controle | 6,015 mV | 6,097 mV |
| Pico de comando (escala software) | 1,5426 V | 1,5777 V |
| Limitação total/integral nas amostras úteis | 0% | 0% |
| Limitação PR nas amostras úteis | 0% | 0% |
| Diferença da estimativa para 0,9 A | aproximadamente −8,2% | −6,34% |

Ambos concluíram os 34 s sem disparar proteção. Nos relatórios de diagnóstico
publicados: sem backlog acumulado, erros de leitura, perda de pool ou overruns
de execução. Máximos observados de execução: 42 us e 41 us. Cada ensaio teve um
deadline overrun com publicação de diagnóstico ativa; isso não constitui pior
caso garantido nem jitter zero. As janelas publicadas não cobrem a fração final
de quatro segundos depois da última janela inteira. A última amostra teve uma
notificação pendente, situação normal (o campo `Last ... backlog=1` registra o
retorno de `ulTaskNotifyTake`, não um acúmulo de mais de quatro).

## Correção numérica entre as partidas

O primeiro relatório calculou RMS ligeiramente menor que a média (0,826416 contra
0,826496 A), por erro de acumulação float na janela DC de 144.000 amostras.
As somas de corrente e quadrados passaram a usar soma compensada (Kahan). Os
controladores e ganhos não mudaram. A repetição passou a reportar RMS coerente.
O teste host ganhou regressão para RMS >= |média|, dentro do arredondamento.
Essa correção numérica é pequena e não explica a diferença de 6–8% para a
referência de corrente. A diferença entre partidas inclui mudança do nível
neutro do sensor e pode envolver deriva física/corrente/escala.

## Decisão sobre os ganhos

**Mantidos Kp=1/Kr60=10 e PID Ki=2.** A integral já reduziu o erro médio para menos
de 0,4 mV e não chegou ao limite. Não há evidência neste ensaio que justifique
maior ganho para corrigir a corrente indicada. Fazer a estimativa dizer 0,900 A
mudando ganhos ou um fator de escala sem nova medição esconderia o problema.

O resultado demonstra compensação DC com pequeno erro residual sob as condições
testadas. Não demonstra precisão de 0,9 A, ganhos ótimos, resposta AC ou estabilidade
para toda a faixa. Próximo passo para a exatidão: conferir corrente primária
**diretamente**, Rs real, referências DAC/condicionador, relação de espiras e zero
com corrente primária nula. A referência 4,95/5,5 é uma estimativa; resistência
sob aquecimento e tensão real no resistor também influenciam a comparação.
Não recalibrar o offset durante a corrente de teste.

## Estado final e registros

A segunda versão está gravada, com os mesmos ganhos. Teste concluído, ADC parado,
quatro DACs no neutro. Um reset repete o teste; não foi habilitada operação contínua.
Não houve escrita de offsets nem de ganhos em NVS. O monitor de captura foi fechado,
liberando a USB. Build, teste host com sanitizadores e `git diff --check` passaram.

- [Primeira gravação](dc-hw-flash.log)
- [Primeiro monitor](dc-hw-monitor.log)
- [Gravação após correção numérica](dc-repeat-flash.log)
- [Monitor da repetição](dc-repeat-monitor.log)
