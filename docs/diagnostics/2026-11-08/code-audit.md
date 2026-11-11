# Revisão do caminho de aquisição e geração

Revisão estática do código ativo, componentes FIR/PR, driver MCP4922 instalado,
integração ADC/GDMA/FreeRTOS no ESP-IDF v6.0 local, diagnósticos, mapa ELF e
configuração efetiva. Foram cruzados os resultados das capturas anteriores.
Esta revisão não altera firmware, configuração ou estado da placa, e não
inclui novos testes em hardware. Uma característica confirmada no código não
é automaticamente uma causa confirmada da deformação analógica.

## 1. Interrupção ADC no Core 0, controle no Core 1 — prioridade alta

`main/main.c:531` cria o handle ADC em `app_main`, cuja afinidade efetiva é
Core 0. No IDF instalado, `adc_continuous_new_handle()` chama
`adc_dma_intr_event_init()`, que registra o callback GDMA e instala a interrupção.
`gdma_install_rx_interrupt()` usa `esp_intr_alloc_intrstatus()`, que escolhe o
core chamador (`esp_cpu_get_core_id()`). Iniciar o ADC depois, no Core 1, não
recria essa alocação.

Consequência: os callbacks ADC e o produtor do ring buffer ficam no Core 0;
a tarefa de controle consome no Core 1. A notificação atravessa cores em cada
frame, e produtor/consumidor usam seções críticas do ring buffer. O worker RMS
compartilha Core 0 com essa interrupção. A afinidade foi deduzida dessa cadeia
do código/configuração; não foi feita uma nova leitura de afinidade em runtime.

É a principal hipótese estrutural para variações de latência. Nos dois eventos
residuais sem logs RMS, os callbacks mantiveram intervalos de 70/69 us e a
etapa compute ficou em 2265/2257 ciclos; o DAC variou 61/78 us. Isso não indica
um pico de cálculo nesses dois frames e é compatível com variação de liberação
ou execução da tarefa. Não prova uma disputa específica de spinlock.

Próxima intervenção indicada: criar/configurar o ADC e sua interrupção em uma
tarefa fixada no Core 1, preservando handles e estado antes de iniciar o ADC,
e repetir a captura sem outras alterações. Confirmar a afinidade real no
startup. Não remover locks do driver.

## 2. Notificação do RMS ocorre dentro do laço crítico — prioridade alta

Em `main/main.c:439`, o controle copia a última amostra de telemetria e chama
`xTaskNotifyGive(rms_task_handle)` após a escrita DAC. A função FreeRTOS entra
na seção crítica global do kernel e pode acordar o worker do outro core.
O processamento RMS, seus logs e a retomada/bloqueio do worker acrescentam
atividade ao mesmo kernel. Separar cores não torna essas operações independentes.

Esse trabalho pode afetar o próximo frame, apesar de ocorrer depois do LDAC
atual. Desabilitar somente os logs preserva essa notificação e toda a captura.
Os testes anteriores, portanto, não isolam os cálculos matemáticos.

Para localizar o custo, registrar em RAM o início e fim da notificação, o
pedido de captura e o início/fim do cálculo, com impressão após a parada. Um
teste posterior pode manter cópias/notificação e omitir somente a matemática.
Não é necessário introduzir logs por evento no laço.

## 3. Protocolo de captura usa volatile entre cores — defeito de sincronização

`main/app_control.c:17` e `main/main.c:471` compartilham `capture_requested` e
`capture_idx`. O worker zera o índice e publica o pedido; o controle lê o flag
e grava os buffers. `volatile` não cria sincronização C nem uma relação
release/acquire para o pedido. A notificação de conclusão ajuda no caminho
controle → worker, mas não substitui o protocolo do pedido no sentido inverso.

Corrigir com propriedade exclusiva do índice pelo controle e pedido publicado
por atomic release/acquire ou outro handoff explícito, com estados para impedir
novo pedido enquanto o buffer é consumido. Validar os limites antes da gravação
na recuperação de estados inválidos. Não adicionar mutex por amostra. É um
problema real de correção, sem evidência de corrupção nas capturas existentes.

## 4. Configuração IRAM pretendida não corresponde à efetiva

`sdkconfig.defaults` usa `CONFIG_FREERTOS_PLACE_FUNCTIONS_INTO_IRAM`, mas o
Kconfig v6.0 local oferece `CONFIG_FREERTOS_IN_IRAM`. O segundo está desabilitado
no `sdkconfig`. O mapa coloca `adc_continuous_read`, `ulTaskGenericNotifyTake`,
`xTaskGenericNotify` e `app_diagnostics_capture_done` em flash. FIR, PR e escrita
LL do DAC estão em IRAM; buffers/LUTs inspecionados estão em RAM interna, sem
PSRAM habilitada.

`SPI_MASTER_IN_IRAM` depende de `FREERTOS_IN_IRAM`, portanto o default SPI também
não realiza toda a intenção descrita nos comentários. O caminho periódico usa
SPI LL, reduzindo a relevância dessa opção específica no regime permanente.
`ADC_CONTINUOUS_ISR_IRAM_SAFE` está desabilitado: um ISR anotado IRAM não torna
esse caminho automaticamente cache-safe.

Há exposição a cache misses e a indisponibilidade de flash, mas não encontramos
escritas/erases de flash no caminho ativo com Wi-Fi/MQTT desativados. Não há
prova de que cache seja a causa residual. Corrigir símbolos/configuração em um
teste separado, verificar orçamento de IRAM e o ELF completo, sem prometer
continuidade durante operações flash.

## 5. Driver DAC tem esperas infinitas e dependência de estado inicial

`managed_components/esp_mcp4922/esp_mcp4922.c:198` e `:210` esperam `cmd.usr`
sem limite. A API LL retorna void, sem possibilidade de reportar timeout.
Uma falha do periférico pode prender a tarefa de prioridade máxima e impedir
novos relatórios; os watchdogs continuam habilitados. Não houve evidência desse
travamento nas capturas, então este é um risco de falha, não explicação comprovada
dos eventos de 61/78 us.

O LL reutiliza o estado SPI estabelecido por uma transmissão inicial pela API
normal. O retorno dessa transmissão e de `spi_device_acquire_bus()` é ignorado
em `main/main.c:258`. Devem ser verificados antes de habilitar aquisição.
A reserva permanente do barramento já existe e deve ser preservada.

O pulso LDAC usa um laço de NOPs. A especificação exige largura mínima de
100 ns ([Microchip, tabela AC](https://ww1.microchip.com/downloads/aemDocuments/documents/OTH/ProductDocuments/DataSheets/22250A.pdf)).
O disassembly atual contém o laço com decremento/desvio, além das operações
GPIO; não são somente dez NOPs consecutivos. Não concluímos que o pulso atual
seja curto demais. Sua margem depende de clock/código gerado e deve ser
validada na borda física se persistir deformação não explicada por software.

## 6. Catch-up e falhas ADC não têm recuperação limitada

`ulTaskNotifyTake(pdFALSE)` preserva notificações; o laço processa frames
pendentes imediatamente, sem limitar a recuperação. Isso pode produzir
escritas DAC agrupadas após atraso. Contadores mostram que não houve backlog
nas capturas recentes, então não explica esses eventos específicos.

O driver ADC chama o callback de frame mesmo quando o envio ao ring buffer
falha; a aplicação conta overflow, mas não ressincroniza notificações e amostras.
Falha de leitura continua o laço, e um frame parcialmente consumido seguido de
erro não tem política de realinhamento. Definir limite de backlog, parada ou
ressincronização para essas falhas, preservando diagnósticos. Não descartar
frames silenciosamente em uma aplicação de controle.

## 7. Partida e medição têm custos diferentes do regime permanente

O ADC é iniciado antes de cinco escritas que ajustam sua cadência; pode produzir
conversões durante essa transição. O FIR preenche os históricos na primeira
amostra, e a primeira execução passa por caminhos frios. Isso oferece hipóteses
para os eventos iniciais, sem determinar sua contribuição individual.

`DAC_done` é timestamp posterior ao retorno da função LL, ao avanço do índice
do seno e a parte da instrumentação. Uma interrupção entre a borda LDAC e o
timestamp pode perturbar a métrica sem deslocar a borda naquela mesma proporção.
A instrumentação também custa CPU por frame. Separar essas medições da borda
física e caracterizar startup e regime permanente em relatórios distintos.

## 8. A demo executa o controle inteiro antes de escrever uma LUT

FIR, PR e conversão das três fases são executados mesmo com `TEST_IS_ON=1`.
A demo substitui seus resultados por uma tabela de senos, mas herda a variação
de duração dessas etapas antes da escrita DAC. Isso é útil para exercitar a
carga real e não é um defeito por si só. Uma variante mínima da demo pode
isolar essa parcela; não deve ser apresentada como correção da malha fechada.

A telemetria registra `val`, não `test_val`: os RMS mostrados anteriormente
não medem os senos efetivamente enviados ao DAC. Isso impede usar os valores
RMS como validação da forma de onda demo.

## Pontos já confirmados e verificações favoráveis

- A publicação periódica no controle causou perturbações correlacionadas às
  fronteiras de janela. Ela está ausente na captura finita atual.
- A emissão dos logs RMS aumentou as pequenas variações nas rodadas comparadas,
  mas desativá-la não eliminou todos os eventos.
- Não há malloc, logs formatados, NVS ou rede por frame no caminho atual; a ISR
  da aplicação não faz ponto flutuante. Wi-Fi/MQTT não são iniciados.
- Os kernels FIR/PR são limitados e sem locks/alocações por amostra; seus testes
  numéricos anteriores passaram. Não surgiu evidência de corrupção numérica.
- CPU está configurada a 160 MHz, PM e PSRAM desabilitados; watchdogs de
  interrupção/tarefa preservados. O tick de 100 Hz e interrupções continuam
  existindo no Core 1. Afinidade não elimina preempções por ISR.

Ordem sugerida: corrigir o protocolo de captura RMS; testar afinidade do ADC
com uma alteração isolada; medir o custo da notificação RMS; corrigir a
configuração IRAM e validar o mapa; tratar timeout/backlog e falhas de driver.
Manter resultados separados para cada mudança, com o mesmo cenário de captura.

## Atualização de implementação

Os itens 1 e 3 foram tratados após esta revisão. A implantação e os testes
isolados estão em [sync-affinity.md](sync-affinity.md). Os demais riscos
registrados aqui continuam exigindo avaliação própria.

Atualização posterior: [polling RMS e correção IRAM](polling-iram.md), com
novas capturas e estado final da configuração.
