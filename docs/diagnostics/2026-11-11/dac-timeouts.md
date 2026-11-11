# Esperas do DAC: correção, regressão e validação

As mudanças do driver foram feitas no repositório original
`../esp32-esp_mcp_4922`, conforme solicitado. A cópia inicialmente criada em
`components/esp_mcp4922` foi removida. O projeto continua consumindo uma
dependência Git pelo Component Manager, com URL pública HTTPS e commit fixo.
Não há path/override local rastreado no manifesto.

## Regressão encontrada e restauração

O primeiro candidato impôs 20 us à chamada LL inteira. Esse critério podia
considerar preparação, leitura de relógio ou preempção como falha de SPI,
mesmo se o hardware tivesse concluído. Na placa, acionou timeout na partida;
a aplicação suspendeu o controle e os três senos desapareceram. Essa versão
não foi considerada validada e não foi publicada.

Foi regravado o driver v0.1.0, retirando o novo tratamento da aplicação para
compatibilizar a API void antiga. O monitor voltou a 14400 frames/s e o usuário
confirmou os três senos no osciloscópio. A imagem funcional foi preservada
para restauração automática em caso de falha nas rodadas seguintes.

## Correção final

- Cada espera pelo bit USR ocupado tem timeout nominal de 20 us. Não há prazo
  global imposto à chamada inteira. Há no máximo `1 + 2 * num_chips` esperas.
- No vencimento, USR é relido. Se o SPI concluiu durante uma preempção da CPU,
  a operação é aceita, evitando o falso timeout do candidato inicial.
- O prazo é medido pelo contador local de ciclos, sem esp_timer na escrita LL.
  O contexto armazena o limite em ciclos calibrado em init. O chamador deve
  permanecer fixado a um core e manter a frequência da CPU constante desde
  a calibração. Nesta aplicação: Core 1, 160 MHz, PM desabilitado. Para outra
  aplicação com DFS, é necessário lock apropriado antes de init; não é automático.
- Timeout real de USR deasserta CS, mantém LDAC alto sem pulso, retorna
  ESP_ERR_TIMEOUT e trava o contexto em falha. Escritas posteriores retornam
  ESP_ERR_INVALID_STATE. Com LDAC controlado, evita latch de dados parciais;
  isso não garante comportamento analógico sob falha elétrica ou reset.
- O retorno LL mudou de void para esp_err_t. O controle agora verifica o erro,
  para o ADC e suspende a tarefa, preservando contexto e reserva de barramento
  até reset. Não tenta escrever novamente nem libera um SPI possivelmente ativo.
- A escrita inicial pela API IDF usa polling_start/end com conclusão limitada
  a um tick; pela comparação `>` do IDF v6.0 pode levar aproximadamente dois
  ticks, mais preempção. O descritor foi movido para o contexto para não ficar
  pendurado na stack após timeout.
- A aquisição inicial e polling_start ainda usam portMAX_DELAY para arbitragem,
  pois o IDF v6.0 não aceita timeout finito nessas APIs. Nesta aplicação, o
  barramento é dedicado e reservado uma vez, sem transações concorrentes,
  fora do laço periódico. Não existe garantia de arbitragem finita para uso
  compartilhado/contendido dessas APIs.

O contador unsigned trata wrap de 32 bits. Quantização, interrupções e
preempções ainda podem ampliar o tempo observado: não se afirma hard real-time
ou um limite global comprovado para a função/loop. Watchdogs foram preservados.

## Testes

Os testes do repositório do driver compilam o código real com SPI/GPIO/clock
emulados e verificam: entrada ocupada, travamento após canais parciais,
ausência de LDAC no erro, fault latch, descritor persistente IDF, wrap,
calibração de ciclos e dois casos que faltavam inicialmente: chamada completa
mais longa que 20 us com transações concluídas e CPU atrasada após conclusão.
Passaram normalmente e com AddressSanitizer/UndefinedBehaviorSanitizer.
O header também passou em verificação sintática C++.

Duas rodadas de 120 s, com RMS e diagnósticos ativos, compararam esp_timer e
contador de ciclos para as esperas individuais:

| Medida | Esperas com esp_timer | Esperas com ciclos (final) |
| --- | ---: | ---: |
| Janelas completas de 5 s | 24 | 24 |
| Frames processados | 1727996 | 1727996 |
| Paradas por timeout DAC | 0 | 0 |
| Backlog / pool_lost / read_errors | 0 / 0 / 0 | 0 / 0 / 0 |
| Execution overruns | 0 | 0 |
| Deadline overruns | 2 | 1 |
| Intervalos DAC_done fora de 62..77 us | 9353 | 4 |
| Intervalos DAC_done (us) | 40..126 | 34..123 |
| Máximo da etapa DAC (ciclos) | 3355 | 1458 |
| Máxima execução reportada (us) | 49 | 46 |

Na rodada final, um evento foi da partida e três ficaram na janela 2,
logo após a primeira publicação dos diagnósticos. As janelas 3..24 não tiveram
intervalos fora da faixa ou overruns. A publicação dos diagnósticos continua
sendo uma perturbação conhecida, fora da medição de execução do ciclo.
O máximo DAC de 1458 ciclos corresponde a aproximadamente 9,1 us a 160 MHz;
na referência restaurada, os máximos de janelas quentes ficaram próximos de
8,6 us. Não são médias nem limites de pior caso.

Após essas rodadas, o usuário confirmou no osciloscópio a presença dos três
senos. Essa confirmação visual não é uma medição quantitativa de jitter.
Falhas reais de hardware SPI não foram injetadas na placa; os casos de
travamento foram emulados no host. O candidato inicial exercitou a parada
real da aplicação por erro, mas foi um falso timeout, não um SPI físico travado.

## Commits e integração

Commits locais no repositório do driver:

- a0cb9ba: limites e fault latch iniciais; candidato que revelou regressão.
- a88e174: timeout por espera e rechecagem de conclusão após preempção.
- aae278a476f2f00a8d7fbb3d3058567e5d0a15ba: contador de ciclos e documentação final.

A captura com ciclos foi feita antes de um ajuste final somente nos comentários
públicos do último commit; o código C da implementação não mudou nesse ajuste.
O build foi repetido com o commit final fixado no manifesto. O driver permanece
limpo, três commits à frente de origin/main, sem push.

O manifesto da aplicação aponta para o commit final via HTTPS. Como ele ainda
não está publicado, o teste local resolveu a mesma URL usando um rewrite Git
somente no ambiente do comando, para ler o repositório original. Não se alterou
configuração Git global nem se rastreou caminho local. O lock ignorado foi
regenerado para abandonar a referência antiga em cache. Builds locais usam o
commit já instalado; outros checkouts precisam que o commit do driver seja
publicado antes de consumir a referência nova.

A integração da nova API e os registros da investigação pertencem ao commit
do projeto principal; os commits do driver são independentes.
`git diff --check` passou nos dois repositórios. A placa permanece em operação
contínua, com logs ativos e a implementação final baseada em ciclos.
