> Modos/ensaios de comissionamento abaixo são históricos. A operação atual está
> em [operation-modes.md](operation-modes.md); flags temporárias foram removidas
> após o snapshot `86a1fe0`.

# PR em paralelo com esp_pid integral

A aplicação usa as APIs públicas de resultado e tracking externo de `esp_pid`
(`6f0e5092a8a86089511c4040ce7396d34c43cef6`, Component Manager) e `esp_pr`
(`107f4ce1235d4288ef7a1481a50dd1f30e7a081f`, submódulo). As URLs continuam
HTTPS públicas. Os commits foram preparados localmente; publique ambos antes
para que outros checkouts consigam resolver essas revisões.

## Validação das APIs públicas (12/11/2026)

Passaram os testes host de PID, PR, coordenador paralelo e ensaio finito com
AddressSanitizer/UndefinedBehaviorSanitizer; também passaram os dois testes de
telemetria/relatório. O build ESP-IDF v6.0 para ESP32-S3 passou. O ELF coloca
`update_result`, `apply_tracking`, seus helpers de cálculo e o coordenador em
`.iram0.text`; isso não demonstra imunidade completa a cache desabilitado nem
substitui medir os prazos no hardware. Esta revisão não foi gravada na placa.

Para validar antes do push, o Component Manager foi executado com um redirecionamento
Git temporário, apenas no ambiente do processo, para buscar o commit PID no
repositório local. `idf.py update-dependencies` foi necessário para atualizar o
lock existente; depois `idf.py build` compilou a revisão nova. Nenhum caminho
local foi adicionado ao manifesto. Publique os commits PID e PR antes de publicar
a integração da aplicação. Os repositórios dos componentes têm commits próprios;
a integração permanece na árvore de trabalho da aplicação junto das alterações
anteriores de comissionamento.

## Coordenação atual PR + integral

Cada fase tem uma instância `app_parallel_t`, atualizada somente pelo proprietário
no Core 1. O modelo PR é compartilhado e imutável; estados/modelos ficam em RAM
interna. Inicialize antes de habilitar a aquisição. A cada frame processado,
`dt=1/14400 s`. Não há alocação, NVS ou formatação de log no caminho periódico.

Ganhos atuais em `main/app_control.h`: Kp=0,5, Ki=10, Kr60=14, Kr120=2,
Kr180=3, Kr300=2 e Kr420=1, com damping de 5 Hz nos ressonantes. O PID usa
Kp=Kd=0; portanto, há um único proporcional. Ganhos e estados não são
persistidos na NVS. O erro analógico já chega formado; ambos os componentes
recebem `reference=erro`, `measurement=0`. A referência de corrente externa é
somente comparação do relatório, não um setpoint digital.

PR e integral agora usam a faixa inteira, sem orçamento reservado. O coordenador
usa os pedidos **não saturados** dos dois componentes, limita sua soma a ±6
Vsoft (DAC 0–4095) e aplica uma correção comum após o cálculo da amostra:

```
requested = requested_PR + requested_I
applied   = clamp(requested, -limit, +limit)
delta     = applied - requested
correction = (1 - exp(-Ts/Ttracking)) * delta
```

`CONFIG_APP_PR_TRACKING_TIME_US` controla esse tracking comum (10 ms atual;
zero desativa a correção). O modelo PR e o PID são inicializados com suas
correções locais desativadas, evitando correção dupla. Os clamps diagnósticos
locais dos componentes não são somados: contribuições opostas podem exceder
individualmente a faixa e ainda produzir uma soma realizável.

Quando existem integral e ramos ressonantes ativos, cada grupo recebe metade
da correção. Dentro do PR, os pesos `correction_share` preparados pelo componente
distribuem sua metade entre os estados `p`; `q` e os históricos seguem sua
atualização normal. Se apenas um grupo dinâmico está ativo, recebe toda a
correção. Proporcional puro não tem estado acumulador a corrigir. Esses pesos
repartem a correção de tracking; ambos os grupos continuam usando toda a faixa.
Sem saturação da soma, delta=0. O comando retornado corresponde ao clamp antes
da quantização do DAC; a quantização não entra na correção anti-windup.

O coordenador usa `esp_pid_update_result()` e `esp_pr_update_result()` para
obter os pedidos, depois chama as respectivas funções `apply_tracking()`.
Não escreve diretamente nos estados dos componentes. O modo externo exige uma
chamada de tracking após cada atualização, inclusive quando a correção é zero;
uma segunda atualização pendente ou tracking duplicado retorna erro. Reset
cancela a pendência. Os snapshots PR mantêm sua convenção pré-correção; o PID
reflete o acumulador corrigido. Os campos `requested_pr`, `requested_integral`,
`requested`, `applied` e `saturation_error` descrevem a soma efetiva da amostra.

As APIs antigas permanecem disponíveis e o modo padrão mantém o tracking local.
As estruturas cresceram: recompile consumidores; inicializadores posicionais
completos devem incluir o novo campo final de modo de tracking.

Falhas numéricas reinicializam os dois grupos e os diagnósticos compartilhados.
A coordenação agora usa `esp_anti_windup`, instalado como submódulo em
`components/esp_anti_windup`. Veja seu [README](../components/esp_anti_windup/README.md)
para API, exemplos e dependências. O componente atualiza participantes, soma,
limita, distribui pesos normalizados na inicialização e reseta estados em falha.
`app_parallel` mantém apenas setup de ganhos e adaptação da telemetria. Continua valendo o
critério de encerramento por saturação sustentada do ensaio finito.

Ensaio com todos ativos: RMS AC estimado 0,537408 A, média DC estimada
−0,481549 A e RMS total 0,721593 A. O usuário informou 0,541 A no multímetro
logo após a captura. A comparação da componente AC dá −0,66%; a comparação
do total dá +33,38%. O modo exato do instrumento não foi confirmado, portanto
as duas grandezas permanecem separadas. O erro médio caiu a −0,072 mV, mas o
residual AC ainda foi 131,240 mV RMS. A sintonia/escala DC e o equilíbrio
instantâneo não estão validados por essa proximidade do RMS AC.
Veja [registro do ensaio](diagnostics/2026-11-12/shared-tracking.md).

## Escala de corrente

Rs=1985 Ω, 2000 espiras de compensação, uma passagem primária e referências
medidas de 3,36 V. A conversão usa o código realmente enviado:

```
VDAC = codigo * Vref_DAC / 4096
Vac  = 2 * VDAC - Vref_condicionador
Icomp_primaria_equivalente = Vac * 2000 / 1985
```

É uma estimativa pela atuação, não uma medição independente da corrente
primária. Exige erro residual suficientemente pequeno e escala/zero corretos.
O RMS total inclui a média DC; RMS somente AC é sqrt(total_RMS² - mean²).

## Faixa completa antes do tracking compartilhado (histórico, 12/11/2026)

O teto reduzido de ±2 Vsoft do ensaio foi removido. O limite efetivo e o default
agora são ±6 Vsoft, correspondentes aos códigos DAC 0–4095; Vsoft continua sendo
uma escala de software, não a tensão física da bobina. Com Ki=0, o PR usa 100%
da faixa e o PID inativo não reserva parcela nem atualiza o integrador. Isso se
aplica tanto ao inicializador normal quanto ao ensaio AC atual. Se Ki for
reativado, os inicializadores ainda usam orçamento repartido para preservar o
anti-windup coordenado existente; não foi implementado tracking externo entre
PR e PID simultaneamente ativos.

No ensaio finito, a parada por saturação agora depende apenas dos extremos
reais do comando DAC: cinco janelas consecutivas de um segundo, cada uma com
pelo menos 10% das amostras em código 0 ou 4095. Uma janela abaixo de 10%
reinicia a contagem. Foram removidas a parada após 100 ms, a parada por
saturação isolada do PID e a rejeição global por mais de 10% de amostras
limitadas. Esse critério indica falta sustentada de atuação; não é uma proteção
elétrica nem altera o anti-windup. Tratamentos de falhas ADC/DAC, backlog e
valores não finitos permanecem. O modo continua finito: 4 s neutro + 60 s de
controle, depois DACs no neutro. A contagem de saturação é específica desse
ensaio; não foi adicionado um novo supervisor de saturação ao modo contínuo.

As faixas teóricas de corrente dependem de Rs, espiras, Vref, ganho analógico e
compliance do amplificador/bobina. Usar todos os códigos do DAC não valida a
faixa física completa nem garante ausência de saturação analógica.

## Ensaio AC anterior com teto reduzido (histórico, 12/11/2026)

`TUNING_AC_TEST=1` seleciona a comparação pelo RMS total, com referência
externa inicialmente informada de 0,560 A RMS, corrigida para 0,549 A RMS
no último ensaio. O ensaio continua finito (4 s no neutro +
60 s de controle, estatísticas dos últimos 10 s). `app_tuning_init_ac()`
reserva 90% do limite ao PR e 10% ao integral, sem aumentar o limite total
±2 Vsoft. A inicialização DC e a operação normal mantêm a divisão padrão
10%/90%. `app_control_init_with_allocation()` permite escolher a divisão
antes de iniciar a aquisição. `app_parallel_init()` exige limites PR
simétricos e menores que o limite total e usa o restante no integral.

Ganhos AC provisórios: Kp=0,5, Kr60=14, Kr120=2, Kr180=3, Kr300=2, Kr420=1 e Ki=0. O integral foi desativado
neste ensaio porque apareceu deslocamento DC médio de aproximadamente
−188 mV em relação ao offset NVS (1,538703 V), levando o orçamento de 10%
do integral à saturação. Isso **não resolve nem valida a medição DC**.
Não recalibrar com corrente primária aplicada. `Icomp_mean` pode estar
próximo de zero em AC; comparar `Icomp_rms` com o instrumento True RMS.
A componente AC residual inclui sinal não compensado, não somente ruído.
Resultados e limitações: [ac-gain-tuning.md](diagnostics/2026-11-12/ac-gain-tuning.md) e
[ac-harmonic-followup.md](diagnostics/2026-11-12/ac-harmonic-followup.md).
O relatório inclui correlações RMS residuais nominais em 60/120/180/240/300/360/420 Hz; não é uma medição da frequência
da rede nem uma identificação de todas as harmônicas.

O modo carrega a NVS sem calibrar, mesmo com GPIO7 baixo, e retorna os DACs
a 2048 ao finalizar ou abortar. Nenhuma sintonia AC foi considerada validada
apenas por aproximar a corrente indicada do valor externo.

## Estado após sintonia DC de 12/11/2026 (histórico)

Firmware atual em ensaio **finito**, não operação contínua: `TUNING_IS_ON=1`,
`TUNING_DC_TEST=1`, `TUNING_HOLD_NEUTRAL=0`, `CALIB_IS_ON=0`, `TEST_IS_ON=0`.
Ganhos atuais Kp=0,2, Kr60=0 e Ki=10, limitados à validação DC feita.
Offset R carregado da NVS: 1,495515 V. Ao reset, faz 4 s no neutro e 60 s de
controle na fase R, depois para e retorna os DACs a 2048. GPIO7 não solicita
calibração neste modo. Resultados em
[powered-gain-tuning.md](diagnostics/2026-11-12/powered-gain-tuning.md).

## Operação contínua anterior (histórico)

`main/main.c`: `CALIB_IS_ON=0`, `TEST_IS_ON=0`, `TUNING_IS_ON=0`,
`TUNING_DC_TEST=0`, `ADC_QUIET_SPI=0`, `ADC_USE_IDF_TIMING=0`.
A malha roda continuamente a 14.400 frames/s, apenas na fase R
(`CONFIG_APP_CONTROL_PHASE_MASK=1`); S/T permanecem no neutro.
O limite normal efetivo e em `sdkconfig.defaults` é 2000 mV de escala de
software (±2 Vsoft), igual ao ensaio DC anterior. Ganhos são definidos em
`main/app_control.h`; nesta transição foram preservados os valores do usuário,
Kp=20, Kr60=10 e Ki=2000. Recompile e grave para aplicar alterações.

O worker do Core 0 captura 100 ms aproximadamente a cada 4,1 s e mostra
`Icomp_mean`, `Icomp_rms`, tensão média/RMS estimada do DAC, extremos dos códigos
e média/RMS/componente AC do ADC e FIR. A tensão do DAC é calculada dos códigos
com Vref=3,36 V; não é uma medição independente da saída analógica. A captura
não cobre continuamente todos os intervalos entre relatórios.

No modo normal, GPIO7 baixo ao reset solicita calibração de offset. Mantenha-o
solto durante ensaios com corrente primária. Wi-Fi/MQTT continuam desativados.

## Ensaio DC finito anterior (histórico)

Estado preparado em `main/main.c`:

```
CALIB_IS_ON=0
TEST_IS_ON=0
TUNING_IS_ON=1
TUNING_DC_TEST=1
```

O teste carrega offsets sem recalibrar, independentemente de GPIO7 baixo; não
inicia RMS, Wi-Fi ou MQTT. Usa aquisição DMA normal e apenas a fase R:

1. DACs no neutro por 4 s, medindo a referência bruta do sensor.
2. Ativa PR+I por 30 s; não recalibra offset com corrente presente.
3. Acumula estatísticas dos últimos 10 s para evitar misturar o transitório com
   a estimativa DC: corrente média **com sinal**, RMS, erro médio/AC, pico de
   comando e frações de limitação de cada ramo.
4. Para o ADC, comanda todos os DACs para 2048 e imprime o relatório. A malha não
   continua automaticamente depois do teste.

O limite específico desse ensaio é ±2 V de comando, aproximadamente ±1,10 A
primários equivalentes no total. Orçamentos: PR ±0,2 V e integral ±1,8 V, isto é,
~±0,11 A e ~±0,99 A respectivamente. Há faixa para testar 0,9 A DC, mas pouca
margem na integral se o offset ou a escala estiverem errados. Isso **não altera**
o limite normal `CONFIG_APP_CONTROL_LIMIT_MV=375` nem habilita S/T.

Proteções do ensaio: valores não finitos, backlog >4, tensão ADC fora de
[0,1 V, 3,0 V], integral/total no limite continuamente por 100 ms ou mais de 10%
das amostras úteis limitadas. A faixa antiga de erro ±0,3 V só permanece nos
ensaios históricos sem corrente. Falha do DAC mantém a política de falha latched;
nesse caso não há garantia de retorno ao neutro nem nova tentativa no contexto.
O relatório de parada inclui separadamente erro bruto, erro filtrado e backlog.

O valor 0,900 A no relatório é apenas a referência informada pelo operador
(4,95 V / 5,5 Ω), usada para comparação final. Não é uma referência aplicada ao
controlador. Com polaridade primária invertida, a corrente média deve trocar de
sinal; a comparação usa a magnitude.

Para testar, preservar a calibração de zero e a montagem/polaridade já verificadas,
aplicar a corrente de referência e executar `idf.py -p PORT flash monitor`.
A integração foi posteriormente gravada e testada em DC: veja os
[resultados em hardware](diagnostics/2026-11-11/pr-pid-dc-hardware.md). A placa
terminou no neutro; um reset repete o ensaio finito.

Para teste AC com o controlador combinado, selecionar `TUNING_DC_TEST=0` (8 s
neutro + 60 s combinado, limite normal de 375 mV software). Para operação contínua,
`TUNING_IS_ON=0`; isso ainda requer validar ganhos, limites e rastreamento na planta.

## Verificação e limites da evidência

Testes de host com o PR e o PID reais passaram com AddressSanitizer e
UndefinedBehaviorSanitizer: compensação ±0,9 A em planta ilustrativa, resposta
DC+60 Hz, soma limitada, saturação prolongada/reversão, independência entre fases,
reinicialização em NaN e conversão de escala. O ensaio DC finito também foi
simulado até concluir 34 s e reportar a janela final de 10 s. A planta simulada
não é um modelo identificado do hardware; esses testes não provam estabilidade.

Testes de handoff RMS e de identificação da polaridade passaram após a integração.
Build ESP32-S3 passou. O ELF coloca `app_parallel_update`, `esp_pid_update` e
`esp_pr_update` em IRAM, estados em RAM interna e `__divsf3` em ROM. Isso não
estabelece imunidade completa a flash/cache nem prazo garantido: o ensaio DC posterior mediu execução e erro residual, mas não estabeleceu
estabilidade/precisão em toda a faixa nem validou AC. Os ensaios anteriores
PR sem integral não validam a nova resposta AC. Veja os
[resultados DC e limitações](diagnostics/2026-11-11/pr-pid-dc-hardware.md).

```sh
idf.py reconfigure build
cmake -S main/tests/parallel -B /tmp/parallel-tests
cmake --build /tmp/parallel-tests
ctest --test-dir /tmp/parallel-tests --output-on-failure
```

No macOS, selecione `CMAKE_OSX_SYSROOT` compatível com o linker se necessário.

Atualização após recalibrar: o novo offset foi carregado, mas a atuação não
reproduziu a resposta anterior. Veja o [reteste e diagnóstico](diagnostics/2026-11-11/recalibrated-offset-test.md)
para o estado mais recente da placa (teste pequeno em malha aberta, terminado
no neutro). Não aumentar ganhos sem identificar a falta de resposta.

Atualização de bancada: [zero e preparação da série de correntes](diagnostics/2026-11-11/zero-before-current-series.md).
`TUNING_HOLD_NEUTRAL=0` aplica controle; `1` mede zero por 84 s com DACs fixos;
`2` também grava o zero estável ao terminar, somente se GPIO7 solicitou calibração
na partida e o operador garantiu corrente primária nula. `TUNING_DC_REFERENCE_A`
é apenas a referência do relatório. O teste DC ativo agora dura 4+60 s, medindo
os últimos 10 s. Não repetir calibração com corrente aplicada.

Referência DAC/condicionador medida posteriormente: **3,36 V**; o código foi
corrigido sem alterar o controle ou recalibrar offsets. Veja o
[primeiro ponto após novo zero](diagnostics/2026-11-11/point09-new-zero.md)
para distinguir corrente estimada, queda real em Rs e referência primária.

Diagnóstico posterior de leitura ADC: [comparação com tensão no GPIO4](diagnostics/2026-11-11/adc-voltage-comparison.md).
`ADC_QUIET_SPI` e `ADC_USE_IDF_TIMING` são flags de bancada restritas a DAC fixo;
precisam ser zero para voltar a controlar. O modo neutro recebe a referência
externa para relatar corretamente ensaios com primário não nulo e não trata a
compensação comandada zero como corrente primária medida.
