# DDS (Direct Digital Synthesis): Um Acumulador de Fase

### 1. O Círculo Mágico de 32 bits

Nossa variável `phase_acc` foi declarada como um inteiro sem sinal de 32 bits (`uint32_t`). Isso significa que ela pode contar de `0` até `4.294.967.295`. Se você somar mais `1`, ela **transborda (overflow)** e volta para o `0` instantaneamente.

Na técnica do DDS, nós dizemos que esse limite astronômico ($2^{32}$) equivale a **1 volta completa no círculo trigonométrico** (0 a 360 graus, ou um ciclo completo da sua onda seno). 

* `0` = 0 graus
* `2147483648` ($2^{31}$) = 180 graus
* `4294967295` (quase $2^{32}$) = 359,999 graus

*(Por isso que o offset da Fase S em 120 graus ali no código é `4294967296 / 3 = 1431655765`!)*

### 2. A Fórmula

Nós queremos que essa variável dê **60 voltas completas por segundo** (ou seja, gere 60 Hz).
Para dar 60 voltas em 1 segundo, o total acumulado no segundo precisa ser:
`60 * 4.294.967.296`

Porém, a nossa rotina não é chamada 1 vez por segundo, ela é chamada `F_loop` vezes por segundo (aproximadamente `13725` vezes).
Então, quanto nós precisamos somar a cada ciclo do loop para que, ao final de 1 segundo, ele tenha dado as 60 voltas cravadas?

Basta dividir a meta pelo número de passos!
**`Incremento (phase_inc) = (Frequência_Desejada * Limite_32_Bits) / Passos_Por_Segundo`**

`phase_inc = (60.0 * 4294967296.0) / F_loop`

### O Trunfo dessa Técnica

Note que nós fazemos a conta de avanço da onda (`phase_inc`) usando **floats** que sobem a casa dos bilhões, mas depois convertemos para um número inteiro puro (`uint32_t`). 
Dentro da interrupção (onde a velocidade importa), **não existe nenhuma matemática de float, divisão ou trigonometria**. É só uma simples soma de inteiros (`phase_acc += phase_inc`). E quando tiramos os 22 bits menos significativos fazendo `phase_acc >> 22`, nós esprememos o número gigantesco de 32 bits em um número de **10 bits (0 a 1023)** para poder extrair o ponto correspondente da nossa tabela `sine_lut` super rápido!

Viewed main.c:228-267

# 

## Os Coeficientes do Controlador PR (Crítico!)

No arquivo `app_control.h` nós temos as constantes:
`PR_B0`, `PR_B1`, `PR_B2`, `PR_A1`, `PR_A2`

Um controlador Proporcional-Ressonante (PR) digital é altamente dependente do tempo de amostragem ($T_s$). Quando você rodou o script no Octave ou MATLAB para gerar esses coeficientes, o software pediu o tempo de amostragem da planta.

* Se você calculou esses coeficientes assumindo $T_s = \frac{1}{13500}$, o pico de ressonância do seu controlador foi projetado para bater em 60 Hz exatos.
* Como o hardware real está rodando com $T_s = \frac{1}{13725.21}$ (ou algo próximo disso), o pico de ressonância do seu controlador atual **não está batendo em 60 Hz**, ele sofreu um leve *shift* (deslocamento) no espectro de frequência.

**O que você deve fazer no futuro:**
Quando formos ligar a malha fechada, você precisará olhar no console qual foi a "Freq de Amostragem Real (F_loop)" que o nosso calibrador imprimiu, ir lá no seu Octave, colocar esse número exato no tempo de amostragem, gerar os novos coeficientes `B0..A2` e atualizá-los no `.h`. Dessa forma, seu PR terá ressonância infinita cravada em 60.00 Hz, em sincronia perfeita com a sua onda!
