# Identificação automática da polaridade da malha

Em `main/main.c`, com `TUNING_IS_ON=0`, `CALIB_IS_ON=1` tem prioridade sobre
`TEST_IS_ON`. Esse modo
identifica a polaridade local do caminho DAC R → bobina → Hall → GPIO4. Não
calibra offsets, não acessa NVS e não verifica GPIO7. Não inicia aquisição DMA,
FIR/PR, RMS, diagnóstico periódico ou Wi-Fi/MQTT. Usa ADC1 oneshot somente no GPIO4.

## Procedimento

Mantenha o condutor primário sem corrente e o circuito conectado, com corrente
de compensação limitada. Grave o firmware e acompanhe o monitor. Todos os DACs
iniciam no código neutro **2048**, seguido de 2 segundos de acomodação.

A rotina executa cinco sequências **neutro → degrau positivo → neutro**:

- Canal R: primeiro MCP4922, CS GPIO10, saída A.
- Patamar positivo: `2048 + CALIB_DAC_STEP_CODES`, atualmente **2176**.
- Outros três canais: sempre neutros.
- Em cada patamar: 300 ms para acomodação, seguidos por 320 conversões ADC,
  divididas em 20 blocos de 16 amostras espaçados em 10 ms (~200 ms/janela).
- Duração nominal total: aproximadamente 10 segundos, incluindo acomodação.
  Não é mais uma onda contínua de 1 Hz; termina após decidir o resultado.

O degrau de 128 códigos é aproximadamente 3,13% da escala. Com ganho unitário e
referência de 3,3 V, corresponde a cerca de 103,1 mV no pino DAC. O código usa a
mesma atenuação, resolução e LUT de tensão calibrada usadas na malha normal.
Não precisa de offsets persistidos: compara o sensor com seu próprio patamar
neutro antes/depois da excitação.

## Critério e resultados

Para cada sequência, calcula:

```
resposta = média_degrau − (média_neutro_antes + média_neutro_depois) / 2
limiar = máximo(5 mV, 3 × ruído)
```

O ruído é a raiz da média das variâncias das três janelas, sem dividir pelo
número de amostras. Isso evita tratar ruído correlacionado como se fosse uma
incerteza arbitrariamente pequena. A deriva entre os dois patamares neutros não
pode exceder o limiar, e as cinco respostas precisam superar o limiar e ter o
mesmo sinal. O monitor imprime as médias, resposta, limiar e deriva por ciclo.

- **COMPATIBLE**: DAC sobe e GPIO4 desce; compatível com realimentação negativa
  usando o sinal atual do controlador.
- **INVERTED**: DAC sobe e GPIO4 sobe; inverter a bobina **ou** o sinal do
  controlador antes de habilitar a malha.
- **INCONCLUSIVE**: resposta pequena, ruído, deriva, sinais inconsistentes,
  leitura próxima dos limites ou falha de periférico. Não aumentar o degrau
  automaticamente nem concluir polaridade a partir desse resultado.

Uma leitura RAW fora de [32, 4063] ou tensão fora de [0,1 V, 3,0 V] interrompe o
teste. Esses critérios são conservadores e não são uma prova estatística de
polaridade. Corrente primária variável, acomodação lenta e interferência
sincronizada podem invalidar a inferência; repita sob condições controladas.
Identificar o sinal local não valida estabilidade ou sintonia do PR.

O sinal do controlador **não é alterado automaticamente** e o resultado não é
gravado em NVS. Ao terminar ou em falha de ADC, o DAC retorna/permanece no neutro
e a malha fica parada. Se ocorrer falha do driver DAC, ele não é reutilizado e
a saída pode conservar o último valor aplicado; um reset é necessário.

## Retorno ao funcionamento normal

Defina `CALIB_IS_ON=0` e `TUNING_IS_ON=0` e recompile/grave. O programa volta a respeitar
`TEST_IS_ON`: `1` gera três senos; `0` aplica o controle. O GPIO7 de calibração de
offsets volta a ser considerado. O valor existente de `TEST_IS_ON=0` foi
preservado: desabilitar `CALIB_IS_ON` habilitará a malha fechada nessa configuração.

## Verificação

O teste host compila `app_polarity.c` real com ADC/DAC/tempo simulados. Exercita
resposta negativa/positiva, resposta fraca, ruído, deriva, inversão entre ciclos,
ADC saturado, falha de leitura e falha DAC. Verifica retorno ao neutro em erro
ADC e ausência de nova tentativa após falha DAC.

```sh
cmake -S main/tests/polarity -B /tmp/polarity-tests
cmake --build /tmp/polarity-tests
ctest --test-dir /tmp/polarity-tests --output-on-failure
```

No macOS, selecione `CMAKE_OSX_SYSROOT` compatível com seu linker, se necessário.
Build ESP32-S3 e teste com AddressSanitizer/UndefinedBehaviorSanitizer passaram.
A rotina foi gravada e executada na placa com degraus de 64 e 128 códigos.
Com 128, as cinco sequências indicaram polaridade compatível; a placa terminou
com todos os DACs no neutro. Veja o [registro de hardware](diagnostics/2026-11-11/polarity-amplitude.md).
Isso não valida estabilidade do controle fechado.
