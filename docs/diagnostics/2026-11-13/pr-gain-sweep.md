# Varredura de ganhos PR e Ki — 13/11/2026

Autorização: variar cada ganho sem perguntar a referência em cada ensaio,
assumindo 0,541 A até o fim. Foram realizadas 23 coletas completas com build,
flash e leitura serial, mais verificação de partida do firmware final. Não
houve recalibração/NVS write: todos carregaram os mesmos offsets R/S/T
1,545657 / 0,513952 / 0,097400 V. Só R tem sensor; os parâmetros são compartilhados
entre fases, com estados independentes. A validação elétrica é apenas de R.

## Resultado deixado no código e na placa

| Parâmetro | Valores experimentados | Valor final |
|---|---|---:|
| Kp | mantido nesta série | 0,5 |
| Ki | 5 / 10 / 20 | 10 |
| Kr60 | 7 / 14 / 21 | 14 |
| Kr120 | 0,5 / 1 / 1,5 / 2 / 3 / 5 | **2** |
| Kr180 | 1,5 / 3 / 4,5 / 6 | 3 |
| Kr300 | 1 / 2 / 3 | 2 |
| Kr420 | 0,5 / 1 / 1,5 | 1 |

A única alteração de ganho mantida foi Kr120 de 1 para 2. CPU 240 MHz,
Fs=14,4 kHz, damping 5 Hz, tracking 10 ms e limites normais não mudaram.
O log de partida agora imprime todos os ganhos PR para conferir a gravação.

## Comparação principal

| Medida na janela detalhada de 100 ms | Baseline inicial | Kr120=2 (primeiro ensaio) | Kr120=2 (confirmação) |
|---|---:|---:|---:|
| RMS AC estimado | 0,533894 A | 0,540768 A | 0,543431 A |
| Diferença relativa à referência ASSUMIDA | −1,31% | −0,043% | +0,45% |
| Residual FIR global RMS AC | 129,913 mV | 121,034 mV | 122,142 mV |
| Bin FIR 120 Hz RMS | 110,031 mV | 99,937 mV | 100,604 mV |

A confirmação reduziu o residual global em 5,98% e o bin 120 Hz em 8,57%
comparados à primeira baseline. Baselines próximas variaram: a redução global
fica aproximadamente em 5–6%, não é uma melhoria rigorosamente isolada de todas
as derivas. Nenhum valor de exatidão/calibração é certificado por esta série.
A referência foi fixada conforme instrução, sem leituras contemporâneas do
multímetro a cada ensaio.

Na confirmação de 75 s, a corrente no último resumo (70 s) foi 0,543394 A;
a tabela usa a captura de 60 s para coincidir com o residual/espectro. As sete
leituras RMS AC dessa confirmação tiveram média 0,542836 A e desvio amostral
0,000635 A. É dispersão observada sob referência assumida, não repetibilidade
metrológica com uma fonte comprovadamente constante. A verificação após a
última gravação indicou 0,541219 A no primeiro resumo de 10 s.

## Como os ganhos foram avaliados

Uma alteração por vez em relação ao conjunto aceito; em casos selecionados,
um candidato passou à confirmação do conjunto. Os ganhos foram comparados
por residual global, bin da frequência correspondente, DC/média, concordância
RMS AC, códigos DAC e saúde da aquisição. A corrente estimada deriva do comando
DAC, não é leitura direta da corrente secundária. RMS total inclui a componente
DC de cerca de 0,5 A; não foi forçado a coincidir com um instrumento em AC.

A triagem automatizada usou limiares conservadores (por exemplo 10% no bin,
5% no global e margem em relação a 0,541 A), que são heurísticas de exploração,
não testes de significância. A revisão final não aceitou automaticamente todos
os candidatos: a redução pequena de Kr300=1 não se repetiu e foi descartada.
Uma regra estrita de 5% também restaurou Kr120=1 após uma confirmação limítrofe;
a revisão conjunta dos dois ensaios de Kr120=2 mostrou a redução repetida de
aproximadamente 5–6% global e 8–9% em 120 Hz, com boa concordância AC. Por isso
Kr120=2 foi selecionado e regravado explicitamente após essa revisão. Os JSONs
de cada etapa preservam as decisões automáticas históricas; a decisão final
está em [pr-final-selection.json](pr-final-selection.json) e o estado realmente
gravado em [pr-final-boot.log](pr-final-boot.log).

Conclusões dos ramos:

- Kr60=21 reduziu seu bin (62,4 -> 45,7 mV), mas aumentou o de 120 Hz e
  afastou a corrente estimada da referência. Kr60=7 piorou o acompanhamento.
- Kr120 concentra o maior residual. Ganhos 3 e 5 reduziram mais esse residual,
  mas elevaram RMS AC estimado para 0,556154 e 0,579185 A. O valor 2 mostrou
  melhor compromisso dentro dos valores testados e foi repetido.
- Kr180 em 4,5/6 trouxe melhorias menores; faltou benefício confirmado do
  conjunto que justificasse alterar o valor original 3.
- Kr300/420 são componentes pequenos e variaram entre ensaios. Ganho aparente
  pequeno não confirmado não foi tratado como melhoria; valores originais.
- Ki=5/20 não demonstrou vantagem consistente frente a 10. Não houve ensaio de
  degrau DC controlado nem referência DC externa para afirmar sintonia ótima.

## Diagnóstico por frequência e temporização

Foram acrescentados bins DFT de ADC/FIR R em 60, 120, 180, 300 e 420 Hz, no
worker Core 0 durante o relatório de 1 minuto. Usam os buffers existentes;
não há novo cálculo por frame no proprietário Core 1. A média é removida e
a magnitude convertida para RMS. Fs nominal e janela de 100 ms são coerentes
com essas frequências; variação da frequência real da rede, leakage e a janela
limitam a interpretação. Não é análise completa de espectro ou banda.

As 23 coletas somaram 20.447.919 ciclos nas janelas registradas. Houve 23 atrasos
de prazo (um no transiente de publicação inicial por coleta), sem watchdog,
backlog, erro ADC ou perda de pool nos dados coletados. Nenhuma captura detalhada
apresentou clipping digital no DAC R. Isso não prova ausência de clipping
analógico no TL084 nem linearidade da relação comando/corrente em todas as
frequências. O máximo de execução observado foi 51 us. Confirmações de 75 s
incluem janelas após o relatório DFT de 60 s, contemplando esse custo eventual.
Máximos observados não são limite de pior caso ou garantia de estabilidade.

Passaram testes host de coordenador PR+I, estatística/FIR e telemetria. O teste
espectral usa DC mais senoides conhecidas de 60/180 Hz, verifica amplitudes RMS,
rejeição de 120 Hz sem sinal e argumentos inválidos. Build/flash final passaram.
Não se adicionou modo de sintonia no firmware; continuam TEST_IS_ON/CALIB_IS_ON.

## Dados e limites

[CSV resumido](pr-sweep-summary.csv), [todas as coletas](pr-sweep-all-trials.json),
[primeira varredura](pr-coordinate-sweep/results.json),
[extensão dos ganhos](pr-coordinate-followup/results.json),
[refinamento](pr-coordinate-refinement/results.json). Cada diretório contém
logs de build, flash e monitor. Espaços finais/quebras foram normalizados.

Os ganhos são provisórios para esta montagem e corrente. Não foi identificado
modelo, margem de estabilidade ou ótimo global. A referência fixa, a possível
deriva da carga, a banda do multímetro e a escala analógica limitam conclusões
na terceira casa decimal. Outros níveis de corrente e transientes ainda não
foram validados; o sensor e atuador de S/T não foram ensaiados eletricamente.
