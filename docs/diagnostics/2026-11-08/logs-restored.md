# Retorno dos logs RMS e dos diagnósticos periódicos

Foram realizados dois testes separados na placa, mantendo -O2, FIR -O3,
FreeRTOS/SPI/ring buffer em IRAM nas opções suportadas, helper de término em
IRAM, ADC/controle no Core 1, handoff atômico e worker RMS por polling.
Não foram alteradas a matemática ou as taxas de aquisição/geração.

1. `APP_RMS_LOG_ENABLE=y`, captura finita de 120 s, sem publicação de
   diagnósticos durante a aquisição: isolou a impressão dos três valores RMS.
2. `APP_DIAG_CAPTURE_SECONDS=0`, RMS e diagnóstico periódico ativos: foram
   registrados os relatórios das 24 primeiras janelas completas de 5 s,
   totalizando 120000056 us de tempo medido. A geração continuou depois.

A referência silenciosa é `ringbuf-iram.log`, da etapa imediatamente anterior.
As flags foram verificadas no sdkconfig e os dois builds/flash passaram, com
verificação de hash. A porta usada foi `/dev/cu.usbmodem831101`.

| Medida | Silencioso, finito | Logs RMS, finito | RMS + diagnóstico periódico |
| --- | ---: | ---: | ---: |
| Tempo medido (s, aproximadamente) | 120 | 120 | 120 |
| Frames processados | 1727995 | 1727995 | 1727996 |
| Intervalos DAC_done (us) | 51..74 | 52..74 | 35..123 |
| Ocorrências fora de 62..77 us | 1 | 1 | 6 |
| Ocorrências após a partida (seq=2) | 0 | 0 | 5 |
| Deadline overruns (incluem latência) | 0 | 0 | 1 |
| Execution overruns (não incluem publicação) | 0 | 0 | 0 |
| Máxima execução reportada (us) | 46 | 44 | 44 |
| Máxima latência callback → tarefa (us) | 15 | 15 | 54 |
| Backlog / pool_lost / read_errors | 0 / 0 / 0 | 0 / 0 / 0 | 0 / 0 / 0 |

## Logs RMS

Foram observadas as impressões RMS durante a captura finita e o relatório
final confirmou `RMS console logging=on`. O único intervalo fora da faixa foi
o frame 2, com 52 us. Não foi observado aumento de irregularidades tardias
nessa rodada em comparação à referência silenciosa. Isso não prova ausência
de impacto em outras cargas ou execuções.

## Diagnóstico periódico

As janelas 1..24 foram recebidas sem lacunas. Os eventos ficaram concentrados
nas três primeiras janelas:

- Janela 1: frame 2, 51 us, evento de partida.
- Janela 2: seq=71997/71998/71999, intervalos 123/35/52 us, imediatamente
  depois da primeira publicação, aproximadamente 5 s após iniciar a aquisição.
- Janela 3: seq=143997/143998, intervalos 78/61 us, perto da segunda publicação
  e do início da primeira rajada de impressão dos diagnósticos.
- Janelas 4..24: nenhum intervalo DAC_done fora de 62..77 us, deadline overrun
  ou execution overrun.

A primeira publicação custou 11322 ciclos, aproximadamente 70,8 us a 160 MHz.
Esse custo é mostrado no relatório seguinte, pois o payload já foi copiado
quando a publicação atual termina. Depois do aquecimento, os custos registrados
foram menores, com várias publicações em 3060 ciclos (aproximadamente 19,1 us).
Não se deve assumir que esse custo menor se manterá sob todas as cargas/cache.

O evento de 123 us aconteceu antes de qualquer impressão do diagnóstico:
a primeira janela é retida pelo reporter até chegar a segunda janela, aos
10 s. Portanto, o maior atraso observado se relaciona ao snapshot/cópia/
publicação no controle, e não exige que o monitor esteja recebendo uma rajada
formatada naquele instante. Os eventos perto de 10 s não isolam publicação e
impressão entre si. Esta comparação também não separa individualmente o custo
de cada operação interna da publicação.

O tempo de publicação é medido depois de fechar a medição do ciclo; por isso
`exec_max=44us` e `cycle_overruns=0` não contradizem um intervalo DAC de 123 us
ou o deadline overrun do frame seguinte. `max_pending=1` também não demonstra
cadência uniforme de saída. Não houve perda de frames ou erro de relatório.

## Limites e estado final

Uma captura por configuração não demonstra pior caso ou ausência de falhas
raras. A diferença entre modos também altera código de diagnóstico e fase do
startup; a correlação com as fronteiras de publicação é a evidência mais forte.
DAC_done continua sendo uma medida por software, sem observação direta da
borda de LDAC ou da forma de onda analógica.

Logs completos: `logs-rms-finite.log` e `logs-continuous.log`. Builds e
`git diff --check` passaram. Os stubs/testes numéricos não foram repetidos:
a alteração efetiva foi de configuração de logs/modo, sem mudança aritmética.
A ajuda Kconfig foi atualizada para descrever o worker atual por polling.

Estado final deliberado: `APP_RMS_LOG_ENABLE=y` e
`APP_DIAG_CAPTURE_SECONDS=0`. A placa permanece gerando continuamente os senos,
com RMS e diagnósticos periódicos ativos. O monitor usado para registrar o
teste foi encerrado sem reiniciar a placa. O custo de publicação continua
presente e ainda merece correção para evitar as perturbações identificadas.
Nenhum commit foi criado.
