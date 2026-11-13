# Integração do esp_anti_windup

Repositório independente: `../esp_anti_windup`, commit `e972cff`.
Consumo pelo submódulo `components/esp_anti_windup`, com URL pública HTTPS.
O histórico do anterior `anti_windup` foi preservado; o núcleo C99 antigo não
é mais usado pela aplicação. [README](../components/esp_anti_windup/README.md)
e [exemplo PID+PR](../components/esp_anti_windup/examples/pr_pid.c).

O componente registra até oito participantes PID/PR e normaliza pesos antes do
loop. Faz atualização, soma não saturada, clamp único, tracking e reset em falha.
A aplicação mantém ganhos, harmônicos, parâmetros de período/limites e a política
de atuação no DAC. Não foram alterados ganhos ou critérios de parada do ensaio.
`app_parallel` é uma camada de setup e adaptação dos nomes da telemetria.
Instâncias contêm ponteiros para participantes: não copie/mova o proprietário.

Validação: testes próprios e integração de controle paralelo/ensaio finito
passaram com ASan/UBSan; ambos os testes de telemetria passaram. Build
ESP-IDF v6.0/ESP32-S3 passou. No ELF, update/reset do coordenador em IRAM,
init em flash. Alpha e pesos são calculados fora do loop; nenhuma promessa de
melhora de latência foi feita sem medição. Esta revisão não foi gravada na placa.

Um consumidor temporário que lista apenas `esp_anti_windup` no manifesto foi
usado para verificar resolução transitiva PID/PR e compilar o exemplo. Commits
ainda locais foram obtidos por reescrita Git restrita ao ambiente do processo;
os manifestos conservam HTTPS público e hashes completos. Não há overrides de
caminhos pessoais nos arquivos rastreados.

Publicação: PID `6f0e509` e PR `107f4ce` primeiro; depois o novo repositório
`smartsensingme/esp_anti_windup` (crie vazio no GitHub se necessário):

```sh
git -C ../esp_anti_windup push -u origin main
```

Não houve criação remota nem push. A integração da aplicação permanece sem
commit junto das alterações anteriores de comissionamento; não foram incluídas
no commit do componente independente.
