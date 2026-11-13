# Primeiro ponto após novo zero — 2026-11-11

O usuário confirmou GPIO7 solto e ~0,9 A reaplicados. Foi gravado e executado o
ensaio de 4 s neutro + 60 s PR+I, somente R, sem gravação de NVS. Offset carregado:
1,578633 V. Rs=1985 Ω, N nominal=2000. Ganhos mantidos: PR Kp=1/Kr60=10 e PID
Kp=0/Ki=2/Kd=0. A estimativa de corrente é pela compensação, não medição direta
independente do primário.

Últimos 10 s ativos:

| Métrica | Resultado |
| --- | --- |
| Compensação média estimada, com Vref configurada=3,30 V | +0,827649 A |
| Compensação RMS estimada, mesma escala | 0,827665 A |
| Erro médio filtrado | −0,426 mV |
| Erro AC RMS filtrado | 5,679 mV |
| Erro filtrado RMS total | 5,695 mV |
| ADC média / RMS total | 1,578210 / 1,578259 V |
| ADC RMS AC | 12,351 mV |
| Pico comando | 1,5320 V software |
| Fração limitada PR / PID | 0% / 0% |

O teste concluiu, retornou os quatro DACs a 2048 e parou o ADC. Não houve motivo
para aumentar ganhos: o erro residual médio já está próximo de zero.

## Medições externas informadas

- DAC neutro: 1,68 V.
- Sobre Rs no neutro: 0 mV, dentro da resolução do instrumento.
- Sobre Rs durante o ensaio: 880 mV.
- Referência do MCP4922, pino 13: **3,36 V**.

A correção de offset no filtro analógico/condicionador pode estabelecer o neutro
bipolar; a subtração digital do offset ADC não produz fisicamente 0 mV sobre Rs.
As alimentações dos amplificadores foram informadas como +7 V/−5 V, compatíveis
com o sinal bipolar do esquema.

O firmware usava 3,30 V como referência DAC/condicionador. A fonte foi corrigida
para 3,36 V em ambos, conforme o Vref comum do esquema. Isso **não altera códigos
DAC, ganhos, dinâmica ou offset ADC**; muda apenas a conversão para corrente.
Não é necessário recalibrar o offset por essa mudança de escala.

Reprocessando o mesmo dado com 3,36 V: 0,827649 × 3,36/3,30 = **0,842697 A**.
Essa é uma reescala do registro, ainda não um segundo ensaio medido. A diferença
para os 0,9 A calculados passa a aproximadamente −6,37%.

A leitura independente de Rs dá 0,880/1985×2000 = **0,886650 A equivalentes**, ou
−1,48% relativo a 0,9 A. Se a corrente primária for exatamente 0,900 A, a leitura
Rs estiver estabilizada e o erro residual for pequeno, isso equivale a **2030
espiras**, não 2119. Não é prova da contagem física. A leitura de Rs não foi
sincronizada com a janela final do firmware; variação da corrente/temperatura e
instantes diferentes precisam ser considerados antes de atribuir a divergência
à conversão DAC. Referência calculada V/R não é amperimetria independente.

## Próximo passo

Solicitada leitura direta da corrente primária em série, mantendo o instrumento
no circuito no próximo ensaio. Aguardar esse valor antes de repetir com a escala
corrigida e comparar outros resistores da **carga primária**. Manter Rs fixo e
N nominal=2000. Um fator constante identifica escala do conjunto, não sozinho o
número de espiras; não aplicar 2119 para forçar concordância com um único ponto.

O build com Vref=3,36 V e testes host de escala/PR+PID/RMS/ensaio finito passaram.
Ele ainda não foi gravado enquanto se aguarda a nova referência de corrente.
A placa permanece no neutro com o ensaio anterior concluído; GPIO7 foi solto.

[Gravação](point09-new-zero-flash.log), [monitor](point09-new-zero-monitor.log),
[medidas estruturadas](point09-new-zero-measurements.json).
