# Ring buffer e helper de captura em IRAM

Foram habilitados `CONFIG_RINGBUF_IN_IRAM=y` no `sdkconfig` efetivo e em
`sdkconfig.defaults`, e aplicado `IRAM_ATTR` à definição de
`app_diagnostics_capture_done()`. A opção foi verificada no Kconfig do ESP-IDF
v6.0 instalado. Não houve alteração no driver ADC, na matemática, no polling
RMS, na afinidade de interrupções/tarefas ou nos watchdogs.

## Verificação do ELF

No binário testado:

- `app_diagnostics_capture_done`: 0x40377ae0, em IRAM.
- Seu literal fica em 0x40374884, em IRAM; o estado `capture_done` está em
  0x3fc9f530, RAM interna. O disassembly contém somente prólogo, carga do
  endereço/estado e retorno, sem chamadas adicionais.
- `xRingbufferReceiveUpTo`: 0x4037e444, em IRAM.
- `vRingbufferReturnItem`: 0x4037e4e8, em IRAM.
- O fragmento linker oficial da opção move texto/literais das funções do
  componente ring buffer para IRAM, incluindo os auxiliares de recepção.
- `.iram0.text` passou de 70591 para 71803 bytes: aumento de 1212 bytes,
  aproximadamente 1,2 KiB. O build cabe na memória disponível.

`adc_continuous_read()` permanece em flash, em 0x42010200 no binário testado.
Não foi auditada/removida toda dependência de flash de caminhos de erro,
assertions e mensagens do driver/ring buffer. Não se afirma que o caminho todo
seja cache-safe ou que a tarefa mantenha prazos durante operações flash.

## Captura de 120 segundos

Condições mantidas: -O2, FIR -O3, CPU 160 MHz, ADC/controle no Core 1, RMS ativo
sem impressão, worker consultando READY a cada tick, captura finita sem
publicação durante aquisição. O monitor ficou conectado e a gravação foi
verificada por hash.

| Medida | Antes (rms-poll-iram.log) | Ring buffer + helper em IRAM |
| --- | ---: | ---: |
| Frames processados | 1727995 | 1727995 |
| Execução máxima reportada, incluindo partida (us) | 82 | 46 |
| Execução máxima em ciclos | 13256 | 7451 |
| Latência máxima callback → tarefa (us) | 33 | 15 |
| Máximo etapa ADC (ciclos) | 7941 | 3558 |
| Máximo etapa compute (ciclos) | 3788 | 2366 |
| Intervalos entre callbacks ADC (us) | 65..73 | 67..70 |
| Intervalos DAC_done (us) | 33..76 | 51..74 |
| Ocorrências fora de 62..77 us | 2 | 1 |
| Ocorrências após os frames 2 e 3 | 0 | 0 |
| Deadline overruns | 1 | 0 |
| Execution overruns | 1 | 0 |
| Backlog / pool_lost / read_errors | 0 / 0 / 0 | 0 / 0 / 0 |

A única ocorrência detalhada foi o frame 2, com intervalo DAC_done de 51 us,
latência de 12 us e compute de 2247 ciclos. Os contadores de timestamp
indisponível/descartado e erros de publicação ficaram em zero. O relatório não
foi publicado durante a aquisição.

O resultado observado é favorável à mudança conjunta. Uma rodada por variante
não prova causalidade individual de ring buffer versus helper, nem fornece
limite de pior caso ou garantia de ausência de eventos raros. Os timestamps
DAC_done continuam sendo uma aproximação por software, sem medição da borda
física ou do sinal analógico. A faixa de 62..77 us é triagem, não especificação
formal de jitter admissível.

## Validação e estado final

Build finito e build adicional do modo contínuo passaram; o contínuo foi
compilado separadamente e não foi executado nesta rodada. O mapa/disassembly
foi inspecionado e `git diff --check` passou. O único ajuste no header documenta
a localização do helper; não houve mudança de API ou aritmética que exigisse
novos testes numéricos.

A placa ficou com esta versão, RMS ativo/silencioso, após concluir a captura
finita de 120 s. O DAC mantém o último valor. Reset inicia outra captura;
`APP_DIAG_CAPTURE_SECONDS=0`, seguido de build/flash, seleciona operação contínua.
A publicação periódica dos diagnósticos continua existente no modo contínuo.
Nenhum commit foi criado.

Teste posterior: [retorno dos logs e operação contínua](logs-restored.md),
com comparação separada de RMS e diagnósticos periódicos.
