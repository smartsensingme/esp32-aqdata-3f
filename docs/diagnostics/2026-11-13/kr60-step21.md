# Ensaio Kr60=21 — 13/11/2026

Alterado apenas Kr60 de 14 para 21, com Kp=0,5, Ki=10 e demais Kr120/180/300/420
em 1/3/2/1. Modelo compartilhado entre fases, mas somente R tem sensor. Mesmos
offsets carregados: 1,545657 / 0,513952 / 0,097400 V. Não houve gravação NVS.
Build, flash e teste host do coordenador passaram.

Aos 60 s: Icomp_mean=+0,516961 A, RMS total=0,791596 A, RMS AC=0,599480 A.
ADC R: média 1,542953 V, RMS AC 131,995 mV; FIR normalizado: média 1,542928 V,
RMS AC 130,836 mV. DAC R: códigos 1837..2939, sem clipping nessa janela.
Coleta serial de aproximadamente 63 s; snapshot elétrico de 100 ms.

Comparação com a confirmação anterior Kr60=14: RMS AC estimado 0,538001 A e
residual FIR 129,521 mV. A carga não foi mantida por fonte rastreável nem houve
captura simultânea, portanto essa comparação não isola perfeitamente o ganho.
O RMS AC de comando aumentou 11,43% e o residual global aumentou 1,02%.
Não foi medido o espectro do residual: estes valores não demonstram a resposta
isolada do harmônico de 60 Hz nem identificam a planta.

Foi solicitada a leitura do multímetro ao fim; ainda não recebida ao registrar
este resultado. Não se calcula erro percentual com uma referência antiga.

12 janelas, 863.996 ciclos, um atraso de prazo no transiente inicial, sem
watchdog, backlog, perda de pool ou erro ADC nos logs. Não há prova de ausência
de oscilação/clipping fora da captura nem de estabilidade em outras cargas.

Como não houve benefício claro no residual global, Kr60=14 foi restaurado
no código e regravado na placa. Kp, Ki, demais ganhos e offsets permanecem.
Não se tentou alterar Ki ou todos os ressonantes simultaneamente. Antes de
novas alterações, convém separar frequências do residual e verificar a relação
entre comando DAC e corrente AC real do secundário; ganho do controlador não
substitui calibração elétrica nem definição AC versus AC+DC do instrumento.

[Log Kr21](kr21-monitor.log); [build](kr21-build.log); [flash](kr21-flash.log).
