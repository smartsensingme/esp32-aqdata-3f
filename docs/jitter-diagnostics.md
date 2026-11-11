# Diagnóstico de jitter pelo monitor serial

A instrumentação em `main/app_diagnostics.c` mantém a geração demo e o
processamento FIR/PR existentes. Wi-Fi/MQTT estão desativados por
`RUN_WITH_WIFI=0`. O período nominal do frame é 1/14400 s (69,444 us).

No modo contínuo, os relatórios de 5 s incluem:

- `wake_max`: tempo do callback ADC até o início da medição na tarefa. Inclui
  o pequeno custo de consumir o timestamp; não mede o atraso do hardware até a
  entrada na ISR. `valid`/`missing` indicam cobertura.
- `ISR`: intervalo entre callbacks correspondentes aos frames consumidos com
  timestamp disponível. Não representa diretamente o instante da conversão ADC.
- `DAC_done`: intervalo entre retornos das escritas no DAC. É uma aproximação
  por software do intervalo de atualização, não uma medição do sinal analógico
  ou da borda física de LDAC.
- `outside`: intervalos fora de 62..77 us, faixa de triagem de aproximadamente
  10% em torno do período nominal; não é especificação de jitter admissível.
- `stage maxima cycles`: leitura ADC, processamento (incluindo parsing e
  preparação), escrita DAC (incluindo seleção do seno) e publicação. As etapas
  incluem preempções e overhead da instrumentação. A publicação mostrada é a
  anterior, pois a atual só termina depois de copiar o relatório.
- `anomaly`: primeiras oito ocorrências por janela, com sequência, instante
  módulo 2^32 us, pendências, latência, intervalos e ciclos de cada etapa.
  `anomalies_dropped` conta as ocorrências adicionais sem detalhes. Os máximos
  e contadores continuam sendo atualizados. `wake_us=4294967295` significa
  timestamp indisponível.

Um FIFO estático SPSC de 64 entradas transfere timestamps e sequências da ISR
para a tarefa. Quando cheio, descarta a entrada nova e incrementa `stamp_drops`
(acumulado desde o boot). A sequência evita associar um timestamp ao frame
errado depois de um descarte. Os slots são publicados com atomics release/acquire.
Cada notificação consumida avança uma sequência, inclusive em falha de leitura.
Perda no pool ADC pode romper a correspondência entre notificação e amostra;
nessa condição, a latência continua sendo da notificação, não da amostra.
Não há alocação nem formatação por frame. O reporter recebe uma cópia limitada
por janela e imprime no Core 0. Sua fila de uma posição pode substituir janelas
se o consumidor não acompanhar; lacunas em `window` devem ser verificadas.

## Captura finita e comparação

Em `menuconfig > Application control`:

- `APP_DIAG_CAPTURE_SECONDS=120`: uma captura de 120 s em RAM. Zero preserva
  os relatórios periódicos. Não há snapshot, cópia de payload nem publicação
  periódica durante a captura finita. Após o último ciclo, a tarefa para o
  ADC, libera o barramento SPI e entrega o relatório imutável a uma tarefa
  no Core 0. O DAC mantém o último valor; reinicie para repetir. A captura
  finita é permitida somente com `TEST_IS_ON=1`.
- `APP_DIAG_DISABLE_RMS=n`: mantém a tarefa RMS e cópias de telemetria.
- `APP_RMS_LOG_ENABLE=n`: desativa somente as três impressões dos valores RMS.
  Com RMS ativo, preserva captura, polling e cálculo. O default Kconfig
  e a configuração atual habilitam a impressão.
- `APP_DIAG_DISABLE_RMS=y`: omite a tarefa RMS e as cópias de telemetria. Mantém
  ADC, FIR/PR e geração dos senos. Esta comparação avalia o conjunto
  RMS/telemetria/logs, sem separar seus efeitos individuais.

Compile/grave cada variante com `idf.py -p PORT flash monitor` e aguarde
`CAPTURE COMPLETE`. O log identifica duração e estado do RMS. O relatório usa
`log-affected` conservadoramente; isso não significa que houve publicação
periódica durante a captura. `publish=0` confirma a ausência desse trabalho.
Os logs de inicialização continuam existindo. Os logs de RMS só existem
quando a tarefa está ativa e `APP_RMS_LOG_ENABLE=y`.

O prazo começa imediatamente antes de iniciar o ADC. A parada ocorre após o
primeiro ciclo que ultrapassa esse prazo; se não chegarem frames, a verificação
ocorre no timeout de espera. O número gerado é lido após parar o ADC e pode
incluir frames que chegaram entre o último ciclo medido e a parada. A duração
usa o fim do último ciclo/timeout, excluindo limpeza e impressão. Os detalhes
retêm somente as primeiras oito anomalias da captura inteira; máximos e
contadores cobrem a captura inteira.

As medições incluem o overhead de timestamps, atomics e atualização de
estatísticas. Seu custo incremental ainda exige comparação específica em
hardware. Elas não detectam diretamente falhas elétricas, analógicas ou o atraso
anterior à entrada da ISR, e máximos observados não constituem limite de pior
caso.

## Otimização do build

A configuração efetiva e `sdkconfig.defaults` selecionam desempenho (`-O2`).
O FIR mantém `-O3` no componente. Resultados medidos e limites da comparação
estão em [optimization-o2.md](diagnostics/2026-11-08/optimization-o2.md).

A captura RMS usa agora um handoff atômico, com índice pertencente ao controle.
O driver ADC é criado pelo controle no Core 1. Validação e limites dos testes
estão em [sync-affinity.md](diagnostics/2026-11-08/sync-affinity.md).

A conclusão da telemetria não notifica mais o worker RMS: ele consulta READY
a cada tick, mantendo o cálculo. Os defaults agora usam o símbolo correto
`FREERTOS_IN_IRAM`, confirmado no sdkconfig/mapa. Resultados e custo de memória
em [polling-iram.md](diagnostics/2026-11-08/polling-iram.md).

`RINGBUF_IN_IRAM` está habilitado e o helper de término da captura está em
IRAM. O ADC read permanece em flash. Ver [ringbuf-iram.md](diagnostics/2026-11-08/ringbuf-iram.md)
para resultados medidos e limites dessa alteração.

Estado atual após os testes: logs RMS e diagnósticos periódicos reativados,
com `APP_DIAG_CAPTURE_SECONDS=0` (geração contínua). O impacto medido está em
[logs-restored.md](diagnostics/2026-11-08/logs-restored.md).

A correção das esperas do DAC, incluindo a regressão inicial e a validação
com osciloscópio/monitor, está em [dac-timeouts.md](diagnostics/2026-11-11/dac-timeouts.md).
