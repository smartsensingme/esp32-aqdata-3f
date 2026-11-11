# Comparação de otimização -Og → -O2

Alteração: `CONFIG_COMPILER_OPTIMIZATION_PERF=y` em `sdkconfig` e
`sdkconfig.defaults`. O FIR preserva seu `-O3` local. Os comandos reais de
compilação confirmam `-O2` para `main.c`, `app_control.c`, `app_diagnostics.c` e
`esp_pr.c`, e `-O2 -O3` para `esp_fir.c` (o último flag prevalece). Não foram
adicionados `-Ofast` ou `-ffast-math`; CPU e taxas do ADC/demo foram mantidas.

Foram repetidas as duas capturas finitas de 120 s, sem publicação periódica,
na mesma placa. A ordem nesta comparação foi RMS desligado, depois ligado.
Os dados de -Og são das capturas anteriores documentadas neste diretório.
A correção que suprime a consulta dos registradores ADC após a parada estava
presente nas capturas -O2; não altera o caminho medido durante a aquisição.

| Medida | -Og RMS on | -O2 RMS on | -Og RMS off | -O2 RMS off |
| --- | ---: | ---: | ---: | ---: |
| Frames processados | 1727994 | 1727995 | 1727994 | 1727995 |
| Máximo da etapa compute (ciclos) | 2917 | 2396 | 2861 | 2391 |
| Execução máxima, incluindo partida (us) | 75 | 74 | 80 | 75 |
| Latência máxima callback → tarefa (us) | 34 | 30 | 43 | 29 |
| Intervalo DAC_done (us) | 39..78 | 34..77 | 39..76 | 33..73 |
| Intervalos DAC fora de 62..77 us | 3 | 11 | 2 | 2 |
| Ocorrências após os frames 2 e 3 | 1 | 9 | 0 | 0 |
| Deadline overruns (incluem latência) | 2 | 1 | 2 | 1 |
| Execution overruns (ciclos) | 1 | 1 | 1 | 1 |
| Backlog / pool_lost / read_errors | 0 / 0 / 0 | 0 / 0 / 0 | 0 / 0 / 0 | 0 / 0 / 0 |
| Publicação durante aquisição (ciclos) | 0 | 0 | 0 | 0 |

O máximo observado da etapa compute caiu 17,9% com RMS ativo e 16,4% sem RMS.
Isso mede a etapa de parsing/condicionamento/FIR/PR/conversão mais overhead de
instrumentação, e não uma média ou redução equivalente de todo o laço. Os
máximos de execução incluem a partida. A taxa permanece próxima de 14400 Hz;
a otimização aumenta a margem de execução, sem alterar o período programado.

A faixa de triagem não apresentou melhora uniforme: com RMS ativo, -O2 teve
nove intervalos curtos (<62 us) depois da partida. Os seis detalhes retidos
após a partida têm DAC_done=61 us. Cinco sequências consecutivas diferem
exatamente 58320 frames, equivalentes a 4,05 s, e os timestamps correspondem
aproximadamente aos logs de RMS, considerando a diferença de origem entre
`esp_timer` e timestamps de log. Essa correlação sugere interferência do
conjunto telemetria/notificação/RMS/logs; não isola qual operação é responsável.
Não houve intervalo acima de 77 us na rodada -O2 RMS on. Os três detalhes
adicionais foram descartados por atingir o limite de oito registros, mas
permanecem incluídos nos contadores e extremos.

Sem RMS/telemetria, apenas os frames 2 e 3 ficaram fora da faixa nas duas
configurações. Uma rodada por variante não estabelece pior caso nem prova
que o sistema esteja livre de falhas raras. A causa analógica/elétrica de uma
possível deformação da saída não é observada por esses timestamps.

O binário da variante sem RMS passou de 256672 bytes (-Og, build anterior) para
245248 bytes (-O2). O binário -O2 com RMS tem 246320 bytes.

## Validação

- Builds ESP-IDF -O2 das duas variantes concluídos, flash verificado por hash.
- Duas capturas de 120 s concluídas: `o2-rms-off.log` e `o2-rms-on.log`.
- Testes numéricos portáveis `esp_pr` em -O2 e `esp_fir` em -O3 passaram no
  host AppleClang. Logs em `pr-o2-host-tests.log` e `fir-o3-host-tests.log`.
  O SDK macOS foi explicitamente selecionado para resolver incompatibilidade
  entre o linker e o SDK padrão local. Não se alterou o ambiente global.
- Mapa ELF mantém `app_control_process_phase`, `esp_pr_update` e
  `esp_fir_process_symmetric` na região IRAM.
- `git diff --check` passou. Não foi executada nova captura do modo contínuo
  com publicações periódicas em -O2.

Estado final: `-O2` em configuração efetiva e defaults, FIR em `-O3`, captura
finita de 120 s, RMS/telemetria ativos. A placa terminou a captura e mantém o
último valor no DAC; reset inicia outra captura. Para voltar à geração
contínua, configurar `APP_DIAG_CAPTURE_SECONDS=0` e compilar/gravar. O custo da
publicação periódica volta a existir nesse modo e não deve ser considerado
resolvido apenas pela mudança de otimização. Nenhum commit foi criado.

Teste posterior: [RMS mantido, impressão desativada](rms-silent.md). Esse teste
atualiza o estado final da configuração de impressão RMS.
