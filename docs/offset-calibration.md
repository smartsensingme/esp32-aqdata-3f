# Calibração dos offsets

O erro de cada fase é tensão ADC menos seu offset. Padrão: +1,65 V. Os offsets são
carregados antes de iniciar aquisição; um registro inválido usa os padrões.

Para calibrar: manter a montagem completa (incluindo Rs de 1,985 kΩ), com corrente
primária nula, ligar GPIO7 ao GND durante o reset e usar `CALIB_IS_ON=0`,
`TUNING_IS_ON=0`. A GPIO é configurável em `CONFIG_APP_OFFSET_CAL_GPIO`. Depois da
mensagem `Calibration saved (DMA)`, liberar a GPIO antes de qualquer reset com
corrente primária. Modos de polaridade e sintonia não gravam offsets.

A solicitação é detectada na partida, mas a captura ocorre na task dona do ADC,
Core 1, pelo caminho contínuo DMA e pelos mesmos filtros e taxa da operação.
Durante 10 s de acomodação e 100 ms de captura, os quatro DACs permanecem em 2048;
o resultado dos controladores é descartado. Não se assume que 2048 elimine todos
os offsets físicos do condicionador; ele é o neutro nominal.

A captura contém 1.440 tensões filtradas por fase, normalizadas pelo ganho DC do
FIR. Descartam-se 144 valores de cada extremidade e calcula-se a média dos 1.152
restantes. A NVS usa um registro versionado de três floats (namespace
`adc_offsets`, chave `record_v1`). Só as fases habilitadas por
`CONFIG_APP_CONTROL_PHASE_MASK` são atualizadas: com R somente, preservam-se S/T.

O ADC é parado e o pool esvaziado antes da ordenação e gravação. Após commit, os
estados FIR/PR/PID são reinicializados antes da aquisição de controle. Não há
alocação ou gravação de offsets durante a operação normal. A captura usa 17.280
bytes temporários de RAM interna, liberados antes de operar.

Erro de ADC/SPI, perda do pool, atraso de mais de quatro frames, amostras inválidas
nas fases habilitadas ou falha de NVS impede iniciar o controle. Falha DAC não é
repetida; contexto fica retido e requer reset. Não se apaga a partição NVS para
resolver erros de inicialização. Persistência sob queda de energia usa as
garantias do NVS, não foi testada fisicamente por estes testes.

Não retirar o sensor para calibrar. Campo residual, deriva térmica, referências,
montagem incompleta e corrente primária não nula podem alterar o zero. O programa
não consegue distinguir esses efeitos de offset eletrônico. Calibrar em montagem
completa e conferir depois média e RMS em zero; não usar RMS total como offset.

A calibração original oneshot foi substituída pela captura operacional para
consistência. Isso não estabelece que DMA, por si, altere a conversão; a causa das
diferenças observadas entre ensaios ainda precisa ser isolada.

Veja [estatísticas RMS e normalização FIR](rms-and-offset.md). Testes host:

```sh
cmake -S main/tests/offsets -B /tmp/offset-tests
cmake --build /tmp/offset-tests
ctest --test-dir /tmp/offset-tests --output-on-failure
```

O teste valida carregamento/solicitação, commit posterior, média aparada,
preservação das fases desabilitadas, dados inválidos e falha de persistência.
Não simula ISR/DMA. A captura DMA bruta foi exercitada na placa; a nova calibração
filtrada/normalizada passou no teste de reconstrução FIR, mas ainda precisa ser
revalidada em hardware.
