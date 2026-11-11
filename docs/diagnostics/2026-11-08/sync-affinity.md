# Sincronização RMS e afinidade ADC

Foram aplicadas duas intervenções, com uma captura de 120 s após cada uma.
Mantiveram-se -O2, FIR -O3, RMS ativo, impressão RMS desativada, captura finita
sem publicação periódica e CPU a 160 MHz.

## Sincronização

Foram removidos os globais volatile de pedido/índice. O índice agora pertence
somente ao controle. Um estado atômico lock-free de 32 bits implementa:

`IDLE → REQUESTED → FILLING → READY → IDLE`

O worker publica o pedido com release; o controle o observa com acquire e
inicia seu próprio índice. O controle publica READY com release após o último
sample; o worker adquire READY antes de ler. O buffer só volta a aceitar pedido
após o cálculo terminar. Pedido duplicado é rejeitado; chamadas de cálculo sem
buffer pronto retornam false sem alterar as saídas. O controle notifica o
worker exatamente quando termina a captura. Não há mutex ou RMW por amostra.

As APIs locais `app_control_request_capture()` e `app_control_calc_rms()`
retornam bool. O worker verifica seus resultados. Buffer, notificação e
matemática RMS continuam presentes, com logs normais RMS desativados.

O teste host compila o app_control real com stubs de headers IDF e verifica
captura incompleta, pedidos sobrepostos, proteção de dados não consumidos e
500 capturas com produtor/consumidor concorrentes. Passou normalmente e com
ThreadSanitizer. Isso não substitui a verificação no target nem prova ausência
de todas as falhas concorrentes. Arquivos em `main/tests/telemetry`.

## Afinidade

A criação/configuração do ADC foi extraída para `init_adc_continuous()`, chamada
pela tarefa de controle fixada no Core 1, antes de habilitar a aquisição. Assim,
a instalação lazy da interrupção GDMA ocorre no core do consumidor.
A calibração e as LUTs continuam prontas antes de criar o controle.

O inventário de interrupções, emitido apenas no startup do modo finito, confirmou
`CPU 1: interrupt 2, level 1, DMA_IN_CH0`. Não se modificou a prioridade da IRQ.
A impressão do inventário ocorre antes de iniciar o ADC e aumenta a duração de
startup; muda a fase do começo da aquisição relativa à tarefa RMS/ticks. Isso
limita uma comparação direta dos eventos de partida e de alinhamentos raros.

## Resultados

| Medida | Antes: volatile, ADC Core 0 | Atômicos, ADC Core 0 | Atômicos, ADC Core 1 |
| --- | ---: | ---: | ---: |
| Log | o2-rms-silent.log | sync-core0.log | sync-core1.log |
| Duração (s) | 120 | 120 | 120 |
| Frames processados | 1727995 | 1727995 | 1727995 |
| Intervalos DAC_done (us) | 34..78 | 34..75 | 34..78 |
| Ocorrências fora de 62..77 us | 4 | 2 | 4 |
| Ocorrências depois dos frames 2 e 3 | 2 | 0 | 2 |
| Latência máxima callback → tarefa (us) | 29 | 36 | 41 |
| Execução máxima incluindo partida (us) | 75 | 79 | 87 |
| Deadline overruns | 1 | 1 | 3 |
| Execution overruns (ciclos) | 1 | 1 | 2 |
| Backlog / pool_lost / read_errors | 0 / 0 / 0 | 0 / 0 / 0 | 0 / 0 / 0 |
| Máximo compute (ciclos) | 3296 | 2368 | 2364 |

Com atômicos e ADC no Core 0, apenas os frames 2/3 apresentaram intervalos fora
da faixa, de 34/44 us. Com ADC no Core 1, os frames 2/3 tiveram 42/34 us, e os
frames 112963/112964 tiveram 78/60 us. Nestes últimos, os callbacks ADC ficaram
em 70/69 us e compute em 2265/2247 ciclos: não se registrou um pico de cálculo
nessas duas amostras. Os máximos e overruns da tabela incluem startup; o detalhe
não localiza temporalmente todos os overruns de execução/deadline.

A correção de sincronização elimina um defeito real de concorrência, mas uma
única captura sem eventos tardios não prova que ela eliminou jitter. A mudança
de afinidade foi confirmada em hardware e não mostrou melhora nesta rodada;
os máximos incluindo partida foram maiores. Portanto, não há base para anunciar
ganho de determinismo ou atribuir a causa exclusivamente à travessia entre cores.
A afinidade Core 1 fica implementada como arquitetura do proprietário ADC,
com benefício de timing ainda não demonstrado e jitter residual em aberto.

Para o próximo isolamento, medir em RAM pedido/fim de captura, entrada/saída de
`xTaskNotifyGive` e início/fim do cálculo RMS. A notificação do worker e o kernel
compartilhado continuam presentes nas duas afinidades. Não inserir formatação
ou impressão por frame. Repetições com cenários comparáveis são necessárias
antes de decidir por afinidade com base em desempenho.

## Validação e estado final

- Builds ESP-IDF do modo finito e do modo contínuo concluídos; o contínuo foi
  compilado em diretório separado e não foi executado nesta rodada.
- Duas capturas de 120 s completas, flash verificado por hash; logs preservados.
- Teste host e teste com ThreadSanitizer passaram; logs em
  `telemetry-host-tests.log` e `telemetry-tsan.log`.
- `git diff --check` passou. Não houve alterações em matemática FIR/PR/RMS,
  frequência de amostragem, flags de otimização ou watchdogs.
- Firmware na placa: sincronização atômica, ADC/Core 1, RMS ativo e silencioso,
  captura de 120 s já concluída; o DAC mantém o último código. Reset inicia
  outra captura. `APP_DIAG_CAPTURE_SECONDS=0` seleciona operação contínua.
- Os demais itens da revisão (IRAM efetiva, timeout SPI, recuperação de backlog
  etc.) permanecem pendentes. Não foi criado commit.

Atualização posterior: [polling RMS e correção IRAM](polling-iram.md), com
novas capturas e estado final da configuração.
