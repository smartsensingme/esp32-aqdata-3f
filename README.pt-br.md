# Arquitetura de Controle Digital de Alta Performance (ESP32-S3)

*Leia em outros idiomas: [English](README.md)*

Uma arquitetura de controle digital de latência ultrabaixa e otimizada em bare-metal para o ESP32-S3. Este projeto demonstra como implementar um **controlador Proporcional-Ressonante (PR) com requisitos temporais** para sistemas de rede trifásica (ex: inversores), acoplado a uma tabela de seno de 60 Hz com taxa fixa e telemetria Wi-Fi/MQTT opcional.

## Arquitetura Central

A interrupção DMA do ADC e a tarefa proprietária do controle ficam no Core 1. O processamento RMS e a apresentação dos diagnósticos ficam no Core 0. A afinidade reduz competição, mas interrupções, locks compartilhados do kernel e flash/cache ainda podem introduzir jitter; os resultados medidos não garantem hard real-time.

- **Core 1:** Processa um frame de três conversões ADC a 14,4 kHz nominal, executa FIR/PR e escreve no DAC. No modo demo, uma LUT de 240 posições fornece três senos de 60 Hz. O barramento SPI dedicado permanece reservado; as esperas pelo hardware têm timeout e os erros são verificados.
- **Core 0:** Usa handoff atômico para a captura RMS e consulta a conclusão uma vez por tick. Executa os logs RMS e a apresentação dos diagnósticos. Wi-Fi/MQTT estão desativados por `RUN_WITH_WIFI=0`; habilitá-los exige nova validação temporal.

## Principais Características

- **Disparo Contínuo via DMA do ADC**: Libera a tarefa de controle por callbacks de frame DMA do ADC. O instante de amostragem é distinto do instante de execução do callback e da tarefa.
- **Cadência exata de 60 Hz por LUT**: O controlador ADC é configurado para 14,4 kHz por fase, produzindo exatamente 240 amostras por ciclo de 60 Hz. Uma LUT de 240 amostras avança uma posição por frame, com deslocamentos de 80 e 160 amostras para as demais fases.
- **Driver SPI DAC em Bare-Metal**: Usa escrita direta nos registradores SPI/GPIO do MCP4922, com reserva exclusiva do barramento, esperas limitadas pelo hardware e tratamento de erro. Os relatórios registram custos medidos e limitações.
- **Controlador Proporcional-Ressonante (PR)**: Componente `esp_pr` multirressonante amortecido, com ganho proporcional comum, sintonia por ramo, saturação e anti-windup configurável. Veja [API, equações e testes](components/esp_pr/README.md).
- **FIR para tempo real**: Componente reutilizável `esp_fir` para filtros FIR reais arbitrários, com núcleos específicos para simetria e antissimetria, históricos espelhados fornecidos pelo chamador, processamento em IRAM e ausência de alocação ou módulo no caminho por amostra. A aplicação chama diretamente o núcleo simétrico para seu filtro de 15 coeficientes.
- **Gestão de Energia Wi-Fi & MQTTS**: A potência de transmissão do Wi-Fi é atenuada (`WIFI_DBPOWER`) e o Power Save é desativado para evitar quedas de tensão (brownouts) no hardware e impedir que o ruído cruzado de RF (cross-talk) corrompa as medições analógicas sensíveis.
- **Diagnóstico de Tempo Real**: `esp_rt_diagnostics` acumula tempos, overruns e eventos na task de controle. `esp_rt_diagnostics_reporter` transfere snapshots para uma task no Core 0 e classifica janelas possivelmente afetadas pelos próprios logs.

## Estrutura do Projeto

* `managed_components/esp_mcp4922/`: Driver MCP4922 obtido por Git e fixado em `main/idf_component.yml`; alterações do código pertencem ao repositório independente do driver.
* `components/esp_pr/`: Controlador PR reutilizável, estados independentes e testes no computador.
* `components/esp_fir/`: Núcleos FIR geral, simétrico e antissimétrico reutilizáveis e testes no computador.
* `main/app_control.c`: Configuração e chamadas diretas ao `esp_pr`, estados das três fases, conversão bipolar para DAC e cálculos de RMS.
* `main/app_fir.c`: Coeficientes FIR e históricos dos filtros das três fases.
* `main/app_diagnostics.c`: Integração do diagnóstico, eventos do ADC e apresentação assíncrona no Core 0.
* `main/main.c`: O orquestrador. Configura a arquitetura dual-core, o DMA do ADC, as conexões Wi-Fi/MQTT e executa a seção crítica de altíssima velocidade.
* `main/secrets.txt`: (Criado pelo usuário) Contém as credenciais de Wi-Fi e MQTT, ignoradas pelo controle de versão.

## Diagnóstico do laço

A cada janela de 5 segundos, o reporter publica contagens da janela e totais desde a inicialização. `pool_lost` é perda confirmada pelo buffer do ADC; `backlog` indica atraso acumulado recuperável; `read_errors` indica que uma notificação não pôde formar um frame completo; e `cycle_overruns` compara o tempo em ciclos de CPU com o período exato de 1/14.400 s. O deadline em microssegundos do componente é arredondado para 70 us, enquanto a comparação em ciclos preserva o limite de aproximadamente 69,44 us.

`quiet` e `log-affected` classificam possível interferência da apresentação; nenhuma classificação prova ausência de jitter. Um FIFO de timestamps com sequência mede a latência callback→tarefa, e resumos de etapas/intervalos ajudam a localizar atrasos. Essas medidas não observam diretamente o instante de conversão ou a borda física de LDAC.

A configuração versionada usa `-O2`, FIR `-O3`, opções suportadas de IRAM para FreeRTOS/SPI/ring buffer, geração demo contínua, RMS e logs ativos. A CPU permanece em 160 MHz com PM desabilitado, conforme o contrato do timeout do DAC por ciclos. Uma falha real de transferência DAC para a aquisição e suspende o proprietário, mantendo a reserva de um SPI possivelmente ativo; é necessário reset.

`APP_DIAG_CAPTURE_SECONDS` seleciona capturas finitas em RAM; `APP_DIAG_DISABLE_RMS` e `APP_RMS_LOG_ENABLE` permitem comparações isoladas. O snapshot/publicação periódico dos diagnósticos ainda perturba o laço, sobretudo no primeiro uso. Veja o [guia de medição e seus limites](docs/jitter-diagnostics.md) e a [validação da regressão/correção do DAC](docs/diagnostics/2026-11-11/dac-timeouts.md).

## Configuração de Hardware

* **Entrada do ADC (ADC1)**: GPIO 4 (Fase R), GPIO 5 (Fase S), GPIO 6 (Fase T)
* **SPI do DAC (SPI2)**: MOSI (GPIO 11), SCK (GPIO 12), LDAC (GPIO 14), CS1 (GPIO 10), CS2 (GPIO 9)

## Como Compilar e Rodar

1. Clone com submódulos (`git clone --recurse-submodules https://github.com/smartsensingme/esp32-aqdata-3f.git`) ou execute `git submodule update --init --recursive` em um checkout existente.
2. Se habilitar Wi-Fi/MQTT, crie o arquivo ignorado `main/secrets.txt` com base em `main/secrets.example.txt`, preenchendo suas credenciais:
   ```c
   #define WIFI_PASS "sua_senha_do_wifi"
   #define MQTT_PASS "sua_senha_do_mqtt"
   ```
3. Ative seu ambiente ESP-IDF v6.0 (`. export.sh`).
4. Compile e grave:
   ```bash
   idf.py build flash monitor
   ```

---
![SmartSensing.me Logo](https://smartsensing.me/ssme-logo.png)

## 📝 Descrição

Este projeto faz parte do ecossistema **SmartSensing.me** e vai além dos exemplos básicos encontrados na internet. Aqui, aplicamos os fundamentos reais da engenharia de instrumentação e sistemas embarcados de alta performance.

Diferente de conteúdos superficiais voltados apenas para cliques, este repositório entrega:
- **Ineditismo:** Implementações originais baseadas em quase 30 anos de experiência acadêmica.
- **Densidade Técnica:** Uso profissional do framework ESP-IDF e FreeRTOS.
- **Didática:** Código documentado e estruturado para quem busca evolução técnica real.

> "Transformamos sinais do mundo físico em inteligência digital, sem atalhos."

---

## 🛠️ Tecnologias
- **Hardware Target:** ESP32-S3
- **Framework:** ESP-IDF v5.x / v6.x
- **Linguagem:** C / C++
- **Simulação:** LTSpice (Modelagem de Sensores)

---

## 👤 Sobre o Autor

**José Alexandre de França** *Professor Adjunto no Departamento de Engenharia Elétrica da UEL*

Engenheiro Eletricista com quase três décadas de experiência no ensino de graduação e pós-graduação. Doutor em Engenharia Elétrica, pesquisador em instrumentação eletrônica e desenvolvedor de sistemas embarcados. O SmartSensing.me é o meu compromisso de elevar o nível da educação tecnológica no Brasil.

- 🌐 **Website:** [smartsensing.me](https://smartsensing.me)
- 📧 **E-mail:** [info@smartsensing.me](mailto:info@smartsensing.me)
- 📺 **YouTube:** [@smartsensingme](https://youtube.com/@smartsensingme)
- 📸 **Instagram:** [@smartsensing.me](https://instagram.com/smartsensing.me)

---

## 📄 Licença

Este projeto está sob a licença MIT.

## Calibração de offset na partida

Os offsets ADC usam +1,65 V por fase como padrão. GPIO7 baixo na partida
solicita calibração e persistência em NVS antes de iniciar a malha. Todos os
DACs iniciam no ponto médio (2048). Veja o [procedimento de calibração](docs/offset-calibration.md)
para ligação, condição de erro zero, configuração e tratamento de falhas.

## Comissionamento do controlador

A sintonia atual é provisória, somente R; veja `main/app_control.h`. A soma
dos comandos pode usar toda a faixa do DAC.
`TUNING_IS_ON=1` executa validação finita e termina com os DACs no neutro;
não entra em controle contínuo. Veja [os modos e resultados](docs/controller-tuning.md).

O controlador atual acrescenta um ramo integral `esp_pid` em paralelo ao PR.
Veja [integração PR/PID e comissionamento](docs/pr-pid-integration.md) para o
anti-windup coordenado da soma, estatísticas AC/DC, escala com Rs medido de
1,985 kΩ e limitações da validação física.

Os relatórios agora separam média, RMS total e RMS AC, inclusive em DC. O offset
usa média aparada após FIR com ganho DC normalizado; não usa RMS como offset.
Veja [RMS e calibração](docs/rms-and-offset.md).
