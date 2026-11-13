> Modos/ensaios de comissionamento abaixo são históricos. A operação atual está
> em [operation-modes.md](operation-modes.md); flags temporárias foram removidas
> após o snapshot `86a1fe0`.

> Atualização de 12/11/2026: o ensaio atual usa a faixa completa do DAC
> (±6 Vsoft, códigos 0–4095). Com Ki=0, PR recebe 100% da faixa. A parada por
> saturação exige cinco janelas consecutivas de 1 s com ≥10% de amostras nos
> extremos do DAC. Os tetos e critérios antigos descritos abaixo são históricos.
> Consulte [pr-pid-integration.md](pr-pid-integration.md).

> A integração atual acrescenta o PID integral em paralelo ao PR. O firmware
> preparado usa o ensaio DC finito de 34 s. Veja [estrutura, limites e procedimento](pr-pid-integration.md).
> Os resultados de hardware vinculados abaixo foram obtidos **antes** da integral.

# Comissionamento limitado da malha R

A sintonia atual é provisória: `APP_CONTROL_KP=1.0f`,
`APP_CONTROL_KR60=10.0f`, amortecimento de 5 Hz. Ganhos em 180/300/420 Hz são
zero. A taxa é 14,4 kHz/fase, mantendo ADC DMA, alinhamento e FIR normais.
Os ganhos ficam em `main/app_control.h`; limite e fases habilitadas estão no
menu **Application control**:

- `CONFIG_APP_CONTROL_PHASE_MASK=1`: R habilitada, S/T neutras mesmo que suas
  entradas estejam flutuantes. Para três sensores conectados, selecionar 7.
- `CONFIG_APP_CONTROL_LIMIT_MV=375`: limite de comando de ±0,375 V na escala
  interna do software. O mapeamento DAC continua representando ±6 V em 0–4095;
  portanto esse limite corresponde aproximadamente a ±128 códigos ao redor
  de 2048. Não é uma medida da tensão/corrente física na bobina.

## Modos temporários em main.c

- `TUNING_IS_ON=1`, `CALIB_IS_ON=0`, `TEST_IS_ON=0`: teste finito de R.
- `TUNING_IS_ON=0`, `CALIB_IS_ON=1`: identificação automática da polaridade.
- `TUNING_IS_ON=0`, `CALIB_IS_ON=0`, `TEST_IS_ON=1`: três senos demo.
- Todos em zero: controle contínuo nas fases habilitadas.

Os modos conflitantes de sintonia são rejeitados na compilação. O modo finito
não inicia RMS/MQTT/Wi-Fi, não calibra nem grava offsets (ignora GPIO7 baixo),
e só carrega a NVS ou usa o padrão quando o registro é inválido/ausente.

`app_tuning_init(false)` prepara nove estágios de 8 segundos: P=0/0,5/1/2/4,
seguidos por P=1 com Kr60=1/2/5/10. O código atual chama
`app_tuning_init(true)`: 8 segundos neutros seguidos por **60 segundos contínuos**
com os ganhos selecionados, sem reinicializar o controlador durante esse período.
Os dois primeiros segundos de cada estágio são descartados das estatísticas.

Os modelos e estados são preparados antes do DMA. A task dona do controle
acumula métricas em RAM sem logs por amostra. Ao terminar, para o ADC, comanda
todos os DACs para 2048 e então imprime o relatório. Não inicia o modo normal
automaticamente depois do ensaio. Os diagnósticos periódicos existentes continuam
ativos; não se assume execução imune a flash/cache ou jitter zero.

O ensaio interrompe por erro ADC, valores não finitos, erro bruto acima de
±0,3 V, backlog maior que 4, limite atingido continuamente por 100 ms, ou mais
de 10% das amostras úteis de um estágio no limite. Falhas do driver DAC mantêm
sua política existente de falha latched: não há nova tentativa nem garantia de
retorno ao neutro nesse caso; é necessário reset. Esses limites não substituem
proteção elétrica externa.

## Interpretação

O relatório contém média do erro filtrado, RMS total/AC, RMS bruto, estimativa
RMS síncrona da componente de 60 Hz, RMS/pico do comando e fração limitada.
Sem corrente primária, mede principalmente ruído, desvio de zero e possíveis
auto-oscilações. Reduzir esses valores não prova rastreamento nem ganhos ótimos.
A estimativa de 60 Hz usa a taxa nominal de frames e não substitui análise
espectral externa ou identificação da planta.

A tensão de zero medida pode diferir do offset salvo por deriva térmica,
condições magnéticas, acomodação ou diferenças entre aquisições. Não corrija
esse desvio aumentando arbitrariamente o ganho e não sobrescreva o offset com
corrente desconhecida. Calibre na condição física correta e confira o zero em
operação antes de ampliar os limites ou a corrente primária.

[Resultados de hardware](diagnostics/2026-11-11/controller-tuning.md).

## Testes host

```sh
cmake -S main/tests/tuning -B /tmp/tuning-tests
cmake --build /tmp/tuning-tests
ctest --test-dir /tmp/tuning-tests --output-on-failure
```

Compila o módulo e o PR reais. Verifica duração, envelope, parada por erro e por
limitação persistente. Também verifica que a validação mantém o estado por 60 s
simulados. Não simula a planta física nem os prazos do scheduler do ESP32.
