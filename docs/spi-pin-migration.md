# Separação dos sinais analógicos e digitais

Placa: [pinout ESP32-S3 NANO SZ](ESP32-S3%20NANO%20SZ%20-%20Pinouts.jpg).
O ADC permanece no ADC1: R=GPIO4, S=GPIO5, T=GPIO6, lado esquerdo.
GPIO7 permanece como solicitação de calibração de offset.

Os cinco sinais dos DACs passam ao lado direito:

| Sinal | GPIO anterior | GPIO novo |
|---|---:|---:|
| MOSI / SDI | 11 | 17 |
| SCK | 12 | 18 |
| CS1 | 10 | 15 |
| CS2 | 9 | 16 |
| LDAC | 14 | 21 |

O host continua SPI2 a 20 MHz, com o mesmo protocolo, canais e ganhos.
Os valores estão no sdkconfig efetivo e em sdkconfig.defaults; o repositório
reutilizável do driver não foi modificado. CS1/CS2/LDAC continuam GPIOs controlados
pelo driver; MOSI/SCK são roteados pela matriz de GPIOs na inicialização SPI.

Mova fisicamente os cinco fios conforme a tabela ANTES de executar o novo
firmware. A imagem SistemaCompleto5.jpg é anterior à troca e mostra os números
antigos; a tabela acima descreve a configuração atual. O arquivo da imagem não
foi redesenhado. Preserve GND comum, fios curtos e retorno próximo aos digitais;
não compartilhe trechos de retorno da bobina com o sensor/ADC.

A alteração foi preparada sem flash. Build não substitui teste físico: validar
primeiro os três senos com TEST_IS_ON=1 e CALIB_IS_ON=0, depois temporização e
medições em malha fechada. A separação física pode reduzir acoplamento, mas não
há medição de melhoria de ruído desta montagem ainda.
