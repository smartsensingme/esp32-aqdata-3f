# Arquitetura de Controle Digital de Alta Performance (ESP32-S3)

*Leia em outros idiomas: [English](README.md)*

Uma arquitetura de controle digital de latência ultrabaixa e otimizada em bare-metal para o ESP32-S3. Este projeto demonstra como implementar um **controlador Proporcional-Ressonante (PR) de tempo real rígido (hard real-time)** para sistemas de rede trifásica (ex: inversores), acoplado a uma tabela de seno de 60 Hz com taxa fixa e telemetria Wi-Fi/MQTT totalmente isolada.

## Arquitetura Central

Para garantir um determinismo absoluto (latência na casa dos microssegundos) sem disparar watchdogs do RTOS ou sofrer engasgos sob tráfego intenso de rede, o sistema utiliza processamento multicore assimétrico:

- **Core 1 (Laço de Controle de Tempo Real):** Completamente isolado do stack de rede. Acionado pelo modo Contínuo do ADC em hardware (via DMA) a 14,4 kHz por fase. Executa a matemática do Controlador Proporcional-Ressonante (PR), avança uma LUT senoidal de 240 amostras e escreve diretamente no FIFO físico do SPI (`mcp4922_ll_write_channels`). O tempo total com anti-windup deve ser medido em bancada.
- **Core 0 (Telemetria e Rede):** Roda o FreeRTOS com o stack de Wi-Fi, TLS e MQTT. Calcula as correntes RMS em janelas móveis de 3 ciclos em background e publica payloads JSON de forma segura (MQTTS), sem roubar nem um ciclo de CPU do laço de controle.

## Principais Características

- **Disparo Contínuo via DMA do ADC**: Substitui as interrupções tradicionais de `gptimer` por callbacks de hardware do DMA, garantindo que o laço de controle esteja em perfeita sincronia com a amostragem analógica do mundo físico.
- **Cadência exata de 60 Hz por LUT**: O controlador ADC é configurado para 14,4 kHz por fase, produzindo exatamente 240 amostras por ciclo de 60 Hz. Uma LUT de 240 amostras avança uma posição por frame, com deslocamentos de 80 e 160 amostras para as demais fases.
- **Driver SPI DAC em Bare-Metal**: Ignora a camada de abstração do ESP-IDF para o DAC Duplo de 12 bits MCP4922. Elimina bloqueios do FreeRTOS e trocas de contexto, reduzindo a latência de transmissão de >100us para **~3us**.
- **Controlador Proporcional-Ressonante (PR)**: Componente `esp_pr` multirressonante amortecido, com ganho proporcional comum, sintonia por ramo, saturação e anti-windup configurável. Veja [API, equações e testes](components/esp_pr/README.md).
- **FIR para tempo real**: Componente reutilizável `esp_fir` para filtros FIR reais arbitrários, com núcleos específicos para simetria e antissimetria, históricos espelhados fornecidos pelo chamador, processamento em IRAM e ausência de alocação ou módulo no caminho por amostra. A aplicação chama diretamente o núcleo simétrico para seu filtro de 15 coeficientes.
- **Gestão de Energia Wi-Fi & MQTTS**: A potência de transmissão do Wi-Fi é atenuada (`WIFI_DBPOWER`) e o Power Save é desativado para evitar quedas de tensão (brownouts) no hardware e impedir que o ruído cruzado de RF (cross-talk) corrompa as medições analógicas sensíveis.
- **Diagnóstico de Tempo Real**: `esp_rt_diagnostics` acumula tempos, overruns e eventos na task de controle. `esp_rt_diagnostics_reporter` transfere snapshots para uma task no Core 0 e classifica janelas possivelmente afetadas pelos próprios logs.

## Estrutura do Projeto

* `components/esp_mcp4922/`: O componente central que contém as implementações de alto nível e bare-metal (`ll`) para o MCP4922.
* `components/esp_pr/`: Controlador PR reutilizável, estados independentes e testes no computador.
* `components/esp_fir/`: Núcleos FIR geral, simétrico e antissimétrico reutilizáveis e testes no computador.
* `main/app_control.c`: Configuração e chamadas diretas ao `esp_pr`, estados das três fases, conversão bipolar para DAC e cálculos de RMS.
* `main/app_fir.c`: Coeficientes FIR, históricos das três fases e benchmark opcional de ciclos no alvo.
* `main/app_diagnostics.c`: Integração do diagnóstico, eventos do ADC e apresentação assíncrona no Core 0.
* `main/main.c`: O orquestrador. Configura a arquitetura dual-core, o DMA do ADC, as conexões Wi-Fi/MQTT e executa a seção crítica de altíssima velocidade.
* `main/secrets.txt`: (Criado pelo usuário) Contém as credenciais de Wi-Fi e MQTT, ignoradas pelo controle de versão.

## Diagnóstico do laço

A cada janela de 5 segundos, o reporter publica contagens da janela e totais desde a inicialização. `pool_lost` é perda confirmada pelo buffer do ADC; `backlog` indica atraso acumulado recuperável; `read_errors` indica que uma notificação não pôde formar um frame completo; e `cycle_overruns` compara o tempo em ciclos de CPU com o período exato de 1/14.400 s. O deadline em microssegundos do componente é arredondado para 70 us, enquanto a comparação em ciclos preserva o limite de aproximadamente 69,44 us.

As janelas `quiet` são a referência para o funcionamento normal. Janelas `log-affected` podem incluir perturbação causada pela apresentação anterior. A latência ISR→task aparece como `unmeasured`: a aplicação drena uma notificação por vez e não mantém uma fila de timestamps por frame, portanto atribuir o último timestamp a frames acumulados produziria uma medida ambígua.

## Comparação do FIR

`APP_FIR_BENCHMARK` executa uma vez na inicialização, antes do ADC e da task de
controle. Ele envia as mesmas amostras determinísticas a três instâncias do
filtro original desenrolado manualmente e a três instâncias de
`esp_fir_process_symmetric()`. O log mostra ciclos por chamada e por frame
trifásico, o tempo equivalente, a diferença relativa de custo e a maior diferença
numérica. O resultado reaparece no primeiro relatório de diagnóstico para não se
perder entre os logs de inicialização. Desabilite a opção depois de obter os resultados para que o linker
remova o filtro antigo e a memória do benchmark. Em seguida, os diagnósticos
normais medem o loop completo de 69,44 us com o novo FIR e a carga real.

## Configuração de Hardware

* **Entrada do ADC (ADC1)**: GPIO 4 (Fase R), GPIO 5 (Fase S), GPIO 6 (Fase T)
* **SPI do DAC (SPI2)**: MOSI (GPIO 11), SCK (GPIO 12), LDAC (GPIO 14), CS1 (GPIO 10), CS2 (GPIO 9)

## Como Compilar e Rodar

1. Clone o projeto.
2. Crie o arquivo `main/secrets.txt` com base no `secrets.example.txt` preenchendo suas credenciais:
   ```c
   #define WIFI_PASS "sua_senha_do_wifi"
   #define MQTT_PASS "sua_senha_do_mqtt"
   ```
3. Ative seu ambiente ESP-IDF v5/v6 (`. export.sh`).
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
- **Hardware Target:** ESP32 / ESP32-S3
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

Este projeto está sob a licença MIT. Veja o arquivo [LICENSE](LICENSE) para detalhes.
