# Novo zero antes da série de correntes — 2026-11-11

O usuário confirmou montagem completa, Rs=1,985 kΩ, corrente primária retirada,
GPIO7 no GND e amplificadores alimentados por +7 V/−5 V. O DAC usa 0/3,3 V e seu
condicionador converte o sinal em bipolar conforme SistemaCompleto4.jpg.
A existência de alimentação bipolar foi confirmada, sem atribuir a ela a falta
de resposta observada com o resistor previamente ausente.

## Calibração inicial pelo caminho filtrado

A primeira calibração filtrada sob DMA gravou R=1,585977 V. Captura: 10 s de
acomodação + 100 ms, média aparada após FIR com ganho DC normalizado. Os relatórios
normais ADC/FIR foram observados na placa e os demais offsets preservados.
Ainda houve diferença do zero no perfil de teste; não se concluiu que DMA altere
a escala nem que o número nominal de espiras explique essa diferença.

[Gravação](filtered-cal-flash.log), [monitor](filtered-cal-monitor.log).
Nos relatórios normais publicados, execução máxima observada 65 us e dois
deadline overruns durante janelas publicadas. O custo de cópia dos novos buffers
não foi isolado dos efeitos de publicação/logs e não se estabeleceu pior caso.

## Medição com DAC fixo

Um teste de 34 s manteve **todos os DACs em 2048**, calculando os controladores
mas descartando a saída. Corrente primária zero. A média ADC mudou de 1,587241 V
na janela inicial para 1,578578 V na janela final (~8,7 mV). Portanto essa variação
ocorreu sem alteração comandada ao atuador. Não foi atribuída a causa exclusiva
a ruído, ADC, referência, montagem ou controlador.

[Teste curto: monitor](neutral-only-monitor.log).

O teste seguinte durou 84 s: 4 s iniciais + oito janelas de 10 s, descartando os
primeiros 2 s de cada janela. Todos os DACs permaneceram em 2048. Médias ADC:
1,578906; 1,578746; 1,578908; 1,578423; 1,578595; 1,578657; 1,578792; 1,578439;
1,578662 V. A amplitude da variação entre médias foi ~0,485 mV; RMS AC ADC ~14 mV.

[Teste longo: monitor](long-neutral-monitor.log).

## Zero gravado no perfil do ensaio

Com GPIO7 ainda no GND e primário sem corrente, foi feito novo ensaio de 84 s
com processamento idêntico ao teste de corrente, mas DACs fixos em 2048. Depois
de parar o ADC, usou-se a média dos erros FIR das **últimas quatro janelas**,
normalizada pelo ganho DC do filtro e somada ao offset usado na captura.
Isso dá uma média de tensão de 32 s úteis, sem usar RMS como offset.

Registro confirmado na NVS:

```
R = 1.578633 V
S = 0.354058 V (preservado)
T = 0.036351 V (preservado)
```

Médias ADC das últimas quatro janelas: 1,578670; 1,578470; 1,578564; 1,578826 V.
Os resíduos impressos nesse relatório foram calculados com o offset antigo:
não indicam que o novo offset tenha sido ignorado. O novo registro só se aplica
à aquisição posterior. A elegibilidade para gravar o zero exige ensaio completo
e estabilidade das médias finais dentro de 2 mV, um critério preliminar, não
uma garantia de exatidão. Fases S/T e ganhos não foram alterados.

[Gravação](zero-profile-flash.log), [monitor](zero-profile-monitor.log).
Os ensaios longos tiveram máximo observado de execução 72 us, incluindo um
overrun de execução na partida, antes das janelas úteis; o limite nominal é
70 us. Não houve perda de pool/erro ADC nos relatórios publicados. Não é garantia
hard real-time; a medida de zero ficou fora do transitório inicial.

## Série proposta

Manter N nominal=2000, Rs=1985 Ω e ganhos PR Kp=1/Kr60=10 + PID Ki=2. Comparar
corrente média de compensação estimada com várias referências DC, exibindo RMS
também. Para ajustar escala DC, usar a média com sinal; RMS inclui ruído.
Variar somente o resistor da carga primária. Conferir referência preferencialmente
por amperímetro; V/R com resistência fria pode diferir da corrente em carga.

Ajustar `I_est = a*I_ref + b`, verificando também retorno a zero. Se b for relevante,
não interpretar razões por ponto como espiras. Se a for constante, isso demonstra
um fator de escala do conjunto; não identifica sozinho N. Referências DAC, ganho
do condicionador, Rs e acoplamento também entram. N equivalente = 2000/a só pode
ser tratado como contagem física se os outros fatores forem verificados.

O antigo valor 2119 foi inferido de um ponto antes de resolver o zero; não foi
aplicado no firmware. A tensão real sobre Rs (ativo e neutro) ajuda a separar
ganho DAC/condicionador de relação magnética, mas ainda não foi fornecida.

## Estado ao preparar o primeiro ponto

A placa terminou no neutro após gravar o zero. O código e o build seguintes estão
preparados para 0,9 A: `TUNING_IS_ON=1`, `TUNING_DC_TEST=1`,
`TUNING_HOLD_NEUTRAL=0`, `TUNING_DC_REFERENCE_A=0.9f`. A etapa ativa passa a 60 s,
com estatísticas dos últimos 10 s e retorno ao neutro. Apenas carrega NVS,
independentemente de GPIO7 baixo. É necessário soltar GPIO7 e confirmar a corrente
reaplicada antes de gravar/executar esse próximo ponto. Nenhum dado de 0,9 A com o
novo zero foi registrado nesta etapa ainda.
