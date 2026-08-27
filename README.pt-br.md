# Arquitetura de Controle Digital de Alta Performance (ESP32-S3)

*Leia em outros idiomas: [English](README.md)*

Uma arquitetura de controle digital de latência ultrabaixa e otimizada em bare-metal para o ESP32-S3. Este projeto demonstra como implementar um **controlador Proporcional-Ressonante (PR) de tempo real rígido (hard real-time)** para sistemas de rede trifásica (ex: inversores), acoplado a uma tabela de seno de 60 Hz com taxa fixa e telemetria Wi-Fi/MQTT totalmente isolada.

## Arquitetura Central

Para garantir um determinismo absoluto (latência na casa dos microssegundos) sem disparar watchdogs do RTOS ou sofrer engasgos sob tráfego intenso de rede, o sistema utiliza processamento multicore assimétrico:

- **Core 1 (Laço de Controle de Tempo Real):** Completamente isolado do stack de rede. Acionado pelo modo Contínuo do ADC em hardware (via DMA) a 14,4 kHz por fase. Executa a matemática do Controlador Proporcional-Ressonante (PR), avança uma LUT senoidal de 240 amostras e escreve diretamente no FIFO físico do SPI (`mcp4922_ll_write_channels`) em `< 4us`.
- **Core 0 (Telemetria e Rede):** Roda o FreeRTOS com o stack de Wi-Fi, TLS e MQTT. Calcula as correntes RMS em janelas móveis de 3 ciclos em background e publica payloads JSON de forma segura (MQTTS), sem roubar nem um ciclo de CPU do laço de controle.

## Principais Características

- **Disparo Contínuo via DMA do ADC**: Substitui as interrupções tradicionais de `gptimer` por callbacks de hardware do DMA, garantindo que o laço de controle esteja em perfeita sincronia com a amostragem analógica do mundo físico.
- **Cadência exata de 60 Hz por LUT**: O controlador ADC é configurado para 14,4 kHz por fase, produzindo exatamente 240 amostras por ciclo de 60 Hz. Uma LUT de 240 amostras avança uma posição por frame, com deslocamentos de 80 e 160 amostras para as demais fases.
- **Driver SPI DAC em Bare-Metal**: Ignora a camada de abstração do ESP-IDF para o DAC Duplo de 12 bits MCP4922. Elimina bloqueios do FreeRTOS e trocas de contexto, reduzindo a latência de transmissão de >100us para **~3us**.
- **Controlador Proporcional-Ressonante (PR)**: Estrutura de controle de nível industrial que rastreia nativamente sinais AC de 60Hz com erro de estado estacionário nulo.
- **Gestão de Energia Wi-Fi & MQTTS**: A potência de transmissão do Wi-Fi é atenuada (`WIFI_DBPOWER`) e o Power Save é desativado para evitar quedas de tensão (brownouts) no hardware e impedir que o ruído cruzado de RF (cross-talk) corrompa as medições analógicas sensíveis.

## Estrutura do Projeto

* `components/esp_mcp4922/`: O componente central que contém as implementações de alto nível e bare-metal (`ll`) para o MCP4922.
* `main/app_control.c`: O cérebro matemático do projeto. Contém as equações do Controlador PR, cálculos de RMS e a geração da Tabela de Seno (LUT).
* `main/main.c`: O orquestrador. Configura a arquitetura dual-core, o DMA do ADC, as conexões Wi-Fi/MQTT e executa a seção crítica de altíssima velocidade.
* `main/secrets.txt`: (Criado pelo usuário) Contém as credenciais de Wi-Fi e MQTT, ignoradas pelo controle de versão.

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
