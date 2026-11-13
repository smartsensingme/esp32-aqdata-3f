# Ensaio inicial de ganhos R — 2026-11-11

Condição informada pelo usuário: sem condutor/corrente primária, somente GPIO4
conectado ao sensor, offset previamente calibrado e polaridade validada.
A ausência de corrente primária não foi medida independentemente pelo agente.

Foram feitos build, gravação com verificação de hash e monitor via USB. Os
ensaios usaram ADC DMA e FIR normais a 14,4 kHz/fase, Core 1, watchdogs ativos.
S/T ficaram no neutro. Comando limitado a aproximadamente ±128 códigos DAC;
nenhum offset foi alterado na NVS. RMS/MQTT não rodaram no modo finito; os logs
e o reporter de diagnósticos continuaram ativos no Core 0.

## Offset carregado

```
R = 1.545959 V
S = 0.338493 V
T = 0.035737 V
```

O registro foi carregado da NVS, sem nova calibração. As leituras S/T não são
usadas para escolher ganhos porque não há sensores nessas entradas.

## Varredura: nove estágios de 8 segundos

Métricas dos últimos 6 s de cada estágio, 86.400 frames úteis por estágio.
Todos tiveram **0% de amostras limitadas**. Kr é somente o ramo de 60 Hz, com
amortecimento de 5 Hz. Não foram excitados os ramos harmônicos superiores.

| Kp | Kr60 | Erro médio (mV) | Erro AC RMS (mV) | Estimativa 60 Hz RMS (mV) | Pico comando (V software) |
| --- | --- | --- | --- | --- | --- |
| 0 | 0 | 16,593 | 5,627 | 0,914 | 0 |
| 0,5 | 0 | 15,144 | 5,614 | 0,960 | 0,0205 |
| 1 | 0 | 14,647 | 5,600 | 0,840 | 0,0393 |
| 2 | 0 | 11,824 | 5,635 | 0,627 | 0,0714 |
| 4 | 0 | 9,916 | 5,705 | 0,615 | 0,1306 |
| 1 | 1 | 13,752 | 5,559 | 0,778 | 0,0369 |
| 1 | 2 | 13,585 | 5,628 | 0,725 | 0,0386 |
| 1 | 5 | 13,367 | 5,647 | 0,523 | 0,0437 |
| 1 | 10 | 13,008 | 5,657 | 0,357 | 0,0511 |

Kp=1/Kr60=10 foi selecionado como ponto inicial: menor componente síncrona de
60 Hz da varredura, comando pequeno e sem limitação. A componente AC total de
~5,6 mV permaneceu quase inalterada. P=4 reduz sobretudo o desvio médio; isso
não é motivo suficiente para substituir a calibração de zero por ganho alto.
A ordem dos estágios não foi aleatorizada e o zero deriva, portanto as diferenças
não devem ser tratadas como comparação estatística controlada.

## Validação: 8 segundos neutro + 60 segundos contínuos

Mesmo ganho selecionado, mantendo o estado ressonante durante todos os 60 s.
Foram descartados apenas os primeiros 2 s de cada estágio.

| Métrica | Neutro (6 s úteis) | Kp=1/Kr60=10 (58 s úteis) |
| --- | --- | --- |
| Frames úteis | 86.400 | 835.200 |
| Erro médio filtrado | 18,818 mV | 15,547 mV |
| Erro filtrado RMS total | 19,673 mV | 16,531 mV |
| Erro filtrado AC RMS | 5,736 mV | 5,620 mV |
| Estimativa síncrona 60 Hz RMS | 1,024 mV | 0,121 mV |
| Comando RMS (escala software) | 0 | 0,0178 V |
| Comando pico (escala software) | 0 | 0,0556 V |
| Fração limitada | 0% | 0% |

As janelas têm durações diferentes: a média síncrona mais longa pode cancelar
componentes incoerentes. Não interpretar a diferença 1,024→0,121 mV como uma
medida precisa de rejeição em frequência. Não há corrente primária conhecida
para avaliar rastreamento ou resposta transitória.

Nos relatórios de diagnóstico publicados, ambos os ensaios tiveram backlog,
erros de leitura, perda de pool e overruns de execução iguais a zero. Máximos
observados de execução: 41 us na varredura e 40 us na validação. Cada ensaio
registrou um deadline overrun durante a operação com publicação de diagnóstico;
essas observações não são limites de pior caso e não representam jitter zero.
Os relatórios publicados em janelas não cobrem integralmente a janela parcial
final depois de parar o ADC.

## Resultado e estado final

Ganhos normais alterados em `main/app_control.h`: Kp=1, Kr60=10 e ganhos dos
harmônicos superiores zero. O limite de comando é configurável e ficou em
±0,375 V software (`CONFIG_APP_CONTROL_LIMIT_MV=375`); a máscara normal ficou
R somente (`CONFIG_APP_CONTROL_PHASE_MASK=1`). Isso não muda a escala de
conversão software ±6 V → DAC 0–4095, nem determina a tensão/corrente da bobina.

A placa foi deixada no modo **finito de validação** (`TUNING_IS_ON=1`,
`CALIB_IS_ON=0`, `TEST_IS_ON=0`), concluído com os quatro DACs no neutro. Um novo
reset repete a validação. Para operação contínua, desabilitar TUNING_IS_ON e
recompilar/gravar; ainda assim, conferir primeiro o desvio de zero observado.
Os ganhos não foram persistidos em NVS e os offsets existentes foram preservados.

Não se demonstrou causa única para a saturação anterior: os ganhos antigos
Kp=5/Kr={60000,6000,10000,13000} são muito maiores, mas não foram reativados em
escala completa para reproduzir a falha. Canais flutuantes e desvio de zero
também podem influenciar o que foi observado. O ensaio demonstra que a nova
configuração não saturou sob as condições testadas, não que seja ótima ou
adequada a qualquer corrente/planta.

Próximo ensaio necessário para sintonia de rastreamento: corrente primária
conhecida, pequena e limitada, com medição de amplitude/fase do erro e da ação.
Não ativar harmônicos adicionais nem ampliar o limite com base apenas no ruído
sem corrente. Rever o offset de R: mesmo no neutro restou erro médio filtrado
entre ~16,6 e 18,8 mV relativo ao valor salvo. Não sobrescrevê-lo com condições
magnéticas/corrente desconhecidas.

## Registros e testes

- [Varredura — gravação](tuning-flash.log)
- [Varredura — monitor](tuning-monitor.log)
- [Validação — gravação](tuning-validation-flash.log)
- [Validação — monitor](tuning-validation-monitor.log)

Build ESP-IDF passou. Testes host de sintonia (PR real, duração/envelope/paradas)
e offsets (incluindo carregar com GPIO baixo sem sobrescrever NVS) passaram com
AddressSanitizer e UndefinedBehaviorSanitizer. O teste de handoff de telemetria
passou após a alteração dos ganhos. Os testes host não simulam a planta.
