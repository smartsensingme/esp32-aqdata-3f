# RMS ativo, impressão desativada

Foi introduzida a opção `APP_RMS_LOG_ENABLE`. Com ela desabilitada no
`sdkconfig`, somente as três chamadas `ESP_LOGI` dos valores RMS são omitidas.
Captura de telemetria, notificação, tarefa, cálculo RMS e retornos pelos
ponteiros de saída permanecem. `APP_DIAG_DISABLE_RMS` continua desabilitado.
O default Kconfig de impressão permanece habilitado para preservar o
comportamento padrão; a configuração efetiva do experimento é silenciosa.

Uma captura de 120 s foi executada na placa, em -O2, FIR -O3 e CPU 160 MHz,
sem snapshots/publicações periódicas. O monitor permaneceu conectado nas
rodadas comparadas; esta alteração avalia a emissão de logs pelo firmware,
incluindo formatação e saída, e não isola o comportamento do programa monitor.

| Medida | RMS ativo com logs (-O2 anterior) | RMS ativo sem logs |
| --- | ---: | ---: |
| Frames processados | 1727995 | 1727995 |
| Intervalo entre callbacks ADC (us) | 62..76 | 67..70 |
| Intervalo DAC_done (us) | 34..77 | 34..78 |
| Intervalos DAC fora de 62..77 us | 11 | 4 |
| Ocorrências fora da faixa após frames 2 e 3 | 9 | 2 |
| Latência máxima callback → tarefa (us) | 30 | 29 |
| Execução máxima incluindo partida (us) | 74 | 75 |
| Máximo etapa compute (ciclos) | 2396 | 3296 |
| Deadline / execution overruns | 1 / 1 | 1 / 1 |
| Backlog / pool_lost / read_errors | 0 / 0 / 0 | 0 / 0 / 0 |
| Publicação durante aquisição (ciclos) | 0 | 0 |

Sem impressão, duas ocorrências são da partida (frames 2 e 3: 34 e 57 us).
As outras duas são consecutivas, nos frames 58243 e 58244, com intervalos
DAC_done de 61 e 78 us, perto da primeira atividade RMS, aproximadamente
4,04 s após o primeiro frame. Os intervalos entre callbacks nesses frames
foram 70 e 69 us; os tempos da etapa compute registrados foram 2265 e 2257
ciclos. Não são picos do máximo de 3296 ciclos. Não houve descarte de detalhes
ou timestamps. A rodada anterior sem o conjunto RMS/telemetria teve somente
as duas irregularidades da partida.

A diminuição de nove para duas ocorrências depois da partida sugere contribuição
da emissão dos logs. Ela não identifica os cálculos RMS como causa da
irregularidade residual: captura, notificação entre cores e execução do worker
continuam presentes. Uma rodada por variante e uma faixa de triagem de 62..77 us
não estabelecem causalidade definitiva nem pior caso. O máximo de compute
maior nesta rodada também impede generalizar melhora em todas as métricas.
Não se mediu diretamente o sinal analógico ou a borda de LDAC.

## Verificação

- Build ESP-IDF concluído; flash com hash verificado.
- ELF inspecionado: `rms_calc_task` chama `app_control_calc_rms`; a função
  mantém o laço de acumulação e três chamadas `sqrtf`, sem chamadas de log.
  O cálculo não foi removido pelo otimizador.
- Captura completa com `RMS=on`, `RMS console logging=off` e
  `CAPTURE COMPLETE`; nenhuma linha `Corrente RMS` no log salvo em
  `o2-rms-silent.log`.
- `git diff --check` passou. Não houve mudança nas expressões matemáticas.

Estado final: -O2, RMS ativo, impressão RMS desativada, captura finita de 120 s
concluída. A placa mantém o último código no DAC; reset inicia outra captura.
Os logs de inicialização e o relatório após parar o ADC permanecem habilitados.
Nenhum commit foi criado.
