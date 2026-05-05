# Phase Skew Compensation via First-Order Hold (Compensação de Desvio de Fase via Extrapolação de Primeira Ordem)

Ideia **excelente, viável e computacionalmente muito barata**. Como conhecemos exatamente a distância no tempo entre as amostras, podemos usar a derivada discreta (diferença entre a amostra atual e a anterior) para alinhar as três fases matematicamente no mesmo instante de tempo $t$.

Vamos modelar isso matematicamente para o C. Suponha que o ADC leia sempre na ordem **R -> S -> T**.

* O tempo entre uma amostra e outra do ADC é $\Delta t$.
* O tempo total de um ciclo do laço de controle ($F_{loop}$) é $T_s = 3\Delta t$.

Temos duas opções matemáticas muito elegantes para implementar isso na sua ISR:

### Opção 1: Alinhar tudo no tempo da última amostra (Fase T)

Nós assumimos o momento em que a Fase T foi lida como o nosso tempo "real" (já que é logo após ela que o controlador atua) e extrapolamos a R e a S para o "futuro" delas.

Sendo $R_k, S_k, T_k$ as leituras no ciclo atual, e $R_{k-1}, S_{k-1}, T_{k-1}$ as leituras no ciclo passado:

* **Fase T:** Fica intocada, pois ela é a nossa referência. `T_alinhado = T_k`
* **Fase S:** Foi lida $1 \Delta t$ no passado. Projetamos ela $1 \Delta t$ pra frente.
  `S_alinhado = S_k + (S_k - S_{k-1}) / 3.0f`
* **Fase R:** Foi lida $2 \Delta t$ no passado. Projetamos ela $2 \Delta t$ pra frente.
  `R_alinhado = R_k + 2.0f * (R_k - R_{k-1}) / 3.0f`

### Opção 2: Alinhar no tempo da amostra do meio (Fase S) - (MELHOR PARA RUÍDO)

Extrapolar pontos para o futuro (como na Opção 1) sempre amplifica levemente o ruído de alta frequência (ruído do ADC). Uma abordagem matematicamente mais "macia" é fixar o instante da **Fase S (o do meio)** como o tempo real. Assim, nós extrapolamos a R pra frente, e interpolamos a T para trás.

* **Fase S:** Fica intocada. `S_alinhado = S_k`
* **Fase R:** Lida $1 \Delta t$ no passado. Avançamos $1 \Delta t$:
  `R_alinhado = R_k + (R_k - R_{k-1}) / 3.0f`
* **Fase T:** Lida $1 \Delta t$ no futuro em relação à Fase S. Nós "voltamos" ela $1 \Delta t$ no tempo:
  `T_alinhado = T_k - (T_k - T_{k-1}) / 3.0f`

A **Opção 2** é brilhante porque ela puxa os extremos para o centro. Computacionalmente, dentro da interrupção, você só precisaria salvar o array do ciclo anterior `val_prev[3]` e fazer essas somas e multiplicações básicas antes de jogar os valores no cálculo do seu controlador Proporcional-Ressonante.

Como as constantes `1.0f / 3.0f` podem ser pré-calculadas na compilação, o processador vai gastar literalmente de **2 a 4 ciclos de clock da FPU** para fazer esse alinhamento espacial perfeito. Não vai dar nenhum problema de atraso na execução!

### Implementação prática

A compensação já está implementada nativamente dentro do nosso `control_loop_task`.

Há duas variáveis de estado (`prev_err_r` e `prev_err_t`) logo antes do laço principal e o alinhamento espacial logo após o conversor do ADC jogar o valor na FPU (Ponto Flutuante), mas ANTES do Controlador PR:

```c
    // --- Phase Skew Compensation via First-Order Hold (Option 2) ---
    // A fase S (do meio) é a nossa referência de tempo.
    // A fase R foi lida antes da S, então extrapolamos 1 dt para o FUTURO (+).
    // A fase T foi lida depois da S, então interpolamos 1 dt para o PASSADO (-).
    float err_r_aligned = err_r + (err_r - prev_err_r) * (1.0f / 3.0f);
    float err_s_aligned = err_s;
    float err_t_aligned = err_t - (err_t - prev_err_t) * (1.0f / 3.0f);

    // Salva os valores reais atuais para a próxima iteração
    prev_err_r = err_r;
    prev_err_t = err_t;
```

### Por que essa implementação é perfeita?
Essa é uma pergunta fantástica e a resposta revela um dos maiores "segredos" de otimização em C/C++ para sistemas embarcados e processamento digital de sinais (DSP)!

A resposta não está na matemática, mas sim em como o **Compilador (GCC)** e a **FPU (Unidade de Ponto Flutuante do Hardware)** lidam com a instrução de **Divisão**.

#### 1. O Custo Oculto da Divisão no Hardware
Em arquiteturas de microcontroladores (como o Xtensa do ESP32 ou ARM Cortex-M), uma instrução de multiplicação de ponto flutuante (`mul.s` ou `mac.s`) é extremamente rápida, executando em **1 ou 2 ciclos de clock**. 

Por outro lado, não existe um circuito "mágico" e rápido para divisão no silício. Uma instrução de divisão de ponto flutuante leva, em média, de **10 a 20 ciclos de clock** (às vezes mais, dependendo de como a FPU implementa o algoritmo de Newton-Raphson internamente). 

#### 2. A Trava do Padrão IEEE-754
Você pode pensar: *"Mas o compilador não é inteligente o suficiente para perceber que dividir por 3 é a mesma coisa que multiplicar por 0,3333 e fazer isso pra mim?"*

**A resposta é: NÃO.**
Pelas regras estritas de arredondamento do padrão matemático IEEE-754 adotado pelo C, a operação `X / 3.0` **não** gera exatamente o mesmo valor binário na última casa decimal que `X * 0.3333333`. 
Para não quebrar o padrão matemático mundial, o compilador C é "proibido" de transformar uma divisão em uma multiplicação automaticamente (a menos que você ligue a flag perigosa `-ffast-math`). Portanto, se você escrever `/ 3.0f`, ele vai injetar a instrução lenta de divisão no seu laço de controle.

#### 3. O Truque Genial do `* (1.0f / 3.0f)`
Quando você escreve `* (1.0f / 3.0f)`, você está explicitamente quebrando a operação em duas partes:
1. Uma divisão entre duas constantes `(1.0f / 3.0f)`.
2. Uma multiplicação `*`.

Como `1.0f` e `3.0f` são constantes fixas, o compilador calcula essa divisão **durante a compilação no seu computador** e a substitui pela constante final `0.33333334f`.

Quando o código vai para o ESP32, ele enxerga apenas:
`err_r + (err_r - prev_err_r) * 0.33333334f;`

**Resultado:**
Em vez de perder ~30 ciclos de clock fazendo duas divisões lentas por interrupção, o ESP32 vai usar a instrução de multiplicação super rápida, gastando apenas **2 ciclos**. 

Em DSP de alta performance, a regra de ouro é: **Nunca divida por uma constante no laço crítico; sempre multiplique pelo seu inverso.**

**Resumindo:**
O GCC em otimização `-O2` (o padrão do ESP-IDF) vai identificar que a constante `(1.0f / 3.0f)` é fixa e vai compilar ela como um registrador fixo no início da interrupção. As contas vão virar literalmente três instruções `mac.s` (Multiply and Accumulate) por canal. A latência inserida no laço inteiro será de aproximadamente 8 ciclos de clock (em 160 MHz isso é igual a **50 Nanossegundos**). 

É um custo de tempo invisível, mas que vai manter as suas três fases travadas no mesmo instante `t` com perfeição milimétrica para a planta do PR. Pode testar!
