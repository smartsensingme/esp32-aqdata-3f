# Retirada da notificação RMS e correção dos defaults IRAM

Duas alterações foram executadas e testadas separadamente em capturas de 120 s.
Mantidos ADC/controle no Core 1, sincronização atômica da captura, -O2, FIR -O3,
RMS ativo sem impressão e ausência de publicações durante a captura finita.

## Worker RMS por consulta periódica

O controle continua copiando a telemetria e publicando o estado READY, mas
não chama mais `xTaskNotifyGive()` para o worker. Foi removido o handle RMS
sem outros consumidores. O worker publica o pedido de captura, consulta
`app_control_calc_rms()` e bloqueia por um tick quando o buffer ainda não está
pronto. Uma consulta sem READY não executa a matemática e não modifica as
saídas. Quando READY é observado, o mesmo cálculo é executado e o buffer é
liberado pelo protocolo atômico existente.

Com `FREERTOS_HZ=100`, o polling adiciona até aproximadamente 10 ms de latência
nominal, mais atraso de escalonamento, entre conclusão do buffer e consumo.
Não é garantia de prazo. O período de 4 s entre cálculos continua usando
`vTaskDelay`, portanto o intervalo de captura pode incorporar esse atraso.
O worker bloqueia entre consultas e preserva tempo para Idle/watchdog. Se a
captura finita parar o ADC com buffer incompleto, o worker continua aguardando
por consultas bloqueantes, sem ocupar continuamente a CPU.

Isso elimina uma chamada ao kernel da conclusão da telemetria no controle,
mas não elimina o kernel ou todos os locks: ADC/notificações do controle,
ring buffer e o próprio polling do worker ainda usam serviços do RTOS.
O ELF confirma chamada ao cálculo e `vTaskDelay` no worker, sem notificação
RMS no corpo da tarefa de controle. A notificação ADC → controle foi mantida.

## Configuração IRAM efetiva

O nome obsoleto `FREERTOS_PLACE_FUNCTIONS_INTO_IRAM` foi substituído por
`FREERTOS_IN_IRAM` em `sdkconfig.defaults`. A configuração efetiva também foi
alterada, pois defaults não substituem escolhas existentes. Foi explicitamente
habilitado `SPI_MASTER_IN_IRAM`, cujo requisito FreeRTOS agora é atendido;
`SPI_MASTER_ISR_IN_IRAM` continua habilitado. O Kconfig selecionou também
`ESP_SPI_BUS_LOCK_FUNCS_IN_IRAM`.

O mapa ELF posiciona `ulTaskGenericNotifyTake`, `vTaskDelay`,
`xTaskGenericNotify` e `spi_device_polling_transmit` em IRAM. A seção
`.iram0.text` passou de 63135 para 70591 bytes: aumento de 7456 bytes,
aproximadamente 7,3 KiB. O build cabe na memória disponível.

Outras chamadas permanecem em flash, incluindo `adc_continuous_read` e
`app_diagnostics_capture_done`; a opção ADC ISR cache-safe não foi alterada.
Essas opções não tornam todo o caminho imune a cache desabilitado, nem
asseguram execução durante suspensão do scheduler/operações flash. O percurso
periódico DAC continua usando a função LL, já localizada em IRAM antes.

## Comparação

| Medida | Notificação RMS, antes | Polling RMS | Polling + IRAM |
| --- | ---: | ---: | ---: |
| Log | sync-core1.log | rms-poll.log | rms-poll-iram.log |
| Duração (s) | 120 | 120 | 120 |
| Frames processados | 1727995 | 1727995 | 1727995 |
| Intervalos DAC_done (us) | 34..78 | 34..74 | 33..76 |
| Ocorrências fora de 62..77 us | 4 | 2 | 2 |
| Ocorrências após frames 2 e 3 | 2 | 0 | 0 |
| Latência máxima callback → tarefa (us) | 41 | 41 | 33 |
| Execução máxima incluindo partida (us) | 87 | 85 | 82 |
| Deadline overruns | 3 | 2 | 1 |
| Execution overruns (ciclos) | 2 | 1 | 1 |
| Máximo compute (ciclos) | 2364 | 2364 | 3788 |
| Backlog / pool_lost / read_errors | 0 / 0 / 0 | 0 / 0 / 0 | 0 / 0 / 0 |

Na rodada polling, os eventos foram exclusivamente seq=2/3, com intervalos de
45/34 us. Na rodada polling+IRAM, foram seq=2/3, com 33/53 us. Todos os
contadores de timestamp ausente/descartado e erros de relatório ficaram em zero.
A publicação durante aquisição permaneceu em zero.

A comparação é favorável à retirada da notificação, mas uma rodada por
variante não demonstra causalidade definitiva ou ausência de falhas raras.
IRAM não trouxe redução adicional na contagem de intervalos fora da faixa;
a latência máxima observada caiu, mas o máximo compute aumentou. Não há base
para afirmar melhora uniforme de todas as métricas. Máximos e overruns incluem
partida; o detalhe não localiza temporalmente todos os overruns.

## Validação e estado final

- Builds finitos das duas variantes concluídos, gravações verificadas por hash.
- Duas capturas completas de 120 s; logs preservados neste diretório.
- Build adicional do modo contínuo com polling e IRAM concluído em diretório
  separado; não foi novamente executado no target nesta rodada.
- Inspeção do ELF/map confirmou polling, preservação do cálculo e posicionamento
  das funções. Matemática e protocolo de buffers não foram alterados; os testes
  concorrentes anteriores do protocolo continuam documentados em sync-affinity.md.
- `git diff --check` passou. Watchdogs e frequência da CPU foram preservados.
- A configuração final usa polling de um tick, ADC Core 1, RMS ativo/silencioso,
  FreeRTOS e SPI nas opções IRAM suportadas, -O2, captura finita de 120 s.
  A placa terminou essa captura e mantém o último valor no DAC; reset inicia
  outra captura. Para operação contínua, selecionar `APP_DIAG_CAPTURE_SECONDS=0`
  e compilar/gravar; a publicação periódica volta a existir nesse modo.
- Nenhum commit foi criado. Persistem os itens de timeout SPI/recuperação de
  backlog e a necessidade de caracterizar a partida e a borda física do DAC.

Atualização posterior: [ring buffer e helper em IRAM](ringbuf-iram.md), com
verificação do ELF, nova captura e estado final atualizado.
