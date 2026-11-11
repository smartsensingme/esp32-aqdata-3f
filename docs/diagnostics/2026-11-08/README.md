# Capturas de jitter — 2026-11-08

ESP32-S3, ESP-IDF v6.0 instalado localmente, CPU 160 MHz, demo trifásica de
60 Hz (`TEST_IS_ON=1`), ADC a 14,4 kframes/s, FIR/PR mantidos, Wi-Fi/MQTT
inativos. Código baseado em `1a7998b` com as alterações locais de diagnóstico.
Uma captura completa de 120 s por variante, sem snapshots ou publicações
periódicas. O RMS ativo inclui suas cópias de telemetria e logs habituais.

| Medida | RMS/telemetria ativos | RMS/telemetria desativados |
| --- | ---: | ---: |
| Duração medida (us) | 120000018 | 120000010 |
| Frames processados | 1727994 | 1727994 |
| Taxa (Hz) | 14399,9 | 14399,9 |
| Backlog / perda no pool / erros de leitura | 0 / 0 / 0 | 0 / 0 / 0 |
| Intervalo entre callbacks ADC (us) | 62..77 | 65..70 |
| Intervalo entre conclusões DAC (us) | 39..78 | 39..76 |
| Intervalos DAC fora de 62..77 us | 3 | 2 |
| Ocorrências fora da faixa após os frames 2 e 3 | 1 (78 us) | 0 |
| Latência máxima callback → tarefa (us) | 34 | 43 |
| Execução máxima, incluindo partida (us) | 75 | 80 |
| Deadline overruns (incluem latência) | 2 | 2 |
| Execution overruns (somente execução em ciclos) | 1 | 1 |
| Publicação durante aquisição (ciclos) | 0 | 0 |
| Timestamps ausentes / descartados | 0 / 0 | 0 / 0 |

Os dois intervalos curtos de partida aparecem explicitamente nos frames 2 e 3
(39 e 41 us nas duas rodadas). Os detalhes não registram o instante de todos
os deadline/execution overruns; seus máximos incluem a partida e não devem ser
interpretados como máximos exclusivos do regime permanente.

Na rodada ativa, o contador gerado é um frame maior que o processado: ele foi
lido depois da parada do ADC, enquanto o tempo e processamento terminam no
último ciclo medido. Não há overflow registrado. A rodada sem RMS teve uma
primeira tentativa interrompida por queda/reset da USB; ela foi descartada e
repetida integralmente. Somente a repetição completa está em `rms-off.log`.

## Interpretação

No log anterior fornecido pelo usuário, seis janelas de 5 s somavam 38
intervalos DAC fora da faixa, com máximo de 128 us. As primeiras amostras após
cada publicação apresentavam atraso, e o custo máximo da publicação era de
11839 ciclos (aproximadamente 74 us). Nas capturas atuais esse trabalho foi
retirado durante a aquisição; não reapareceu o padrão periódico de pausas
seguido de intervalos curtos observado nas fronteiras das janelas.

A publicação periódica é a principal perturbação identificada pelos dados.
A diferença residual entre RMS ativo e inativo é somente uma ocorrência em
120 s por variante; uma única rodada não estabelece causalidade nem garante
ausência de falhas raras. Permanecem limitações da medição por software, da
partida e da ausência de observação elétrica/analógica.

Os logs contêm avisos de clock ADC não reconhecido após a parada, pois a versão
executada ainda tentava ler registradores desinicializados no relatório final.
Eles ocorrem depois da medição e não indicam falha de aquisição. O código final
suprime essa consulta em modo finito; a correção de apresentação foi compilada,
sem repetir as capturas de 120 s.

## Validação e estado final

- Builds das duas variantes finitas concluídos e gravados, com verificação de
  hash pelo esptool.
- Duas capturas completas de 120 s, relatórios preservados em `rms-on.log` e
  `rms-off.log`.
- Build adicional do modo contínuo com RMS ativo concluído em diretório
  separado; esse modo não foi novamente gravado/testado em hardware.
- Código final compilado em modo finito e contínuo; `git diff --check` passou.
- `sdkconfig` seleciona `APP_DIAG_CAPTURE_SECONDS=120` e
  `APP_DIAG_DISABLE_RMS=y`. Os defaults Kconfig continuam sendo modo contínuo
  e RMS ativo. A placa ficou com a captura sem RMS concluída e DAC mantendo o
  último valor; reset inicia outra captura de 120 s.
- Nenhum commit foi criado.

## Comparação posterior de otimização

Os testes seguintes com `-O2` estão documentados em
[optimization-o2.md](optimization-o2.md). O estado final descrito acima é o
encerramento da primeira comparação em `-Og`; a comparação posterior registra
a configuração atualizada.

A comparação isolando somente a impressão RMS está em
[rms-silent.md](rms-silent.md), com o estado final mais recente.

As correções seguintes de sincronização e afinidade ADC, com resultados
isolados e estado final atualizado, estão em [sync-affinity.md](sync-affinity.md).

Atualização posterior: [polling RMS e correção IRAM](polling-iram.md), com
novas capturas e estado final da configuração.

Atualização posterior: [ring buffer e helper em IRAM](ringbuf-iram.md), com
verificação do ELF, nova captura e estado final atualizado.

Teste posterior: [retorno dos logs e operação contínua](logs-restored.md),
com comparação separada de RMS e diagnósticos periódicos.
